/*!
 *  Copyright (c) 2018 by Contributors
 * \file allreduce_base.cc
 * \brief Basic implementation of AllReduce
 *
 * \author Ankun Zheng
 */
#include "comm/communicator_base.h"
#include <cstdlib>
#include <cstring>
#include <map>
#include "common/env.h"
#include "common/threadpool.h"
#include "core/logging.h"
#include "sys/error.h"
#include "sys/network.h"
#include "transport/channel.h"
#include "utils/string_utils.h"
#include "utils/topo_utils.h"
#ifdef RDC_USE_RDMA
#include "transport/rdma/rdma_channel.h"
#endif
namespace rdc {
namespace comm {
// constructor
Communicator::Communicator(const std::string& name) {
    name_ = name;
    version_number = 0;
    // tracker URL
    err_link = nullptr;
    children_counter_ = 0;
    is_main_comm_ = true;
}
Communicator::Communicator() : Communicator(kMainCommName) {
}
Communicator::Communicator(const Communicator& other) {
    num_neighbors_ = other.num_neighbors_;
    tree_neighbors_ = other.tree_neighbors_;
    parent_rank_ = other.parent_rank_;
    tree_map_ = other.tree_map_;
    is_main_comm_ = false;
}

Communicator::~Communicator() {
}
// initialization function
void Communicator::Init(int world_size, int num_conn, int num_accept) {
    // clear the setting before start reconnection
    //---------------------
    // start
    CHECK_F(all_links_.size() == 0, "can only call Init once");
    this->BuildTopology(world_size);
    // get information from tracker
    this->ReConnectLinks(std::make_tuple(num_conn, num_accept));
}

// register communicator to tracker
void Communicator::Register() {
    Tracker::Get()->Lock();
    Tracker::Get()->SendStr(std::string("register"));
    Tracker::Get()->SendStr(name_);
    Tracker::Get()->UnLock();
}
void Communicator::Shutdown() {
    // notify tracker rank i have shutdown
    this->Barrier();
    Tracker::Get()->Lock();
    Tracker::Get()->SendStr(std::string("shutdown"));
    Tracker::Get()->UnLock();
}
void Communicator::Barrier() {
    this->Exclude();
    Tracker::Get()->Lock();
    Tracker::Get()->SendStr(std::string("barrier"));
    Tracker::Get()->SendStr(name_);
    std::string barrier_token;
    Tracker::Get()->RecvStr(barrier_token);
    CHECK_EQ(barrier_token, "barrier_done");
    Tracker::Get()->UnLock();
    this->UnExclude();
}
void Communicator::Exclude() {
    std::string lock_token;
    do {
        Tracker::Get()->Lock();
        Tracker::Get()->SendStr(std::string("exclude"));
        Tracker::Get()->SendStr(name());
        Tracker::Get()->RecvStr(lock_token);
        Tracker::Get()->UnLock();
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    } while (lock_token != "exclude_done");
}
void Communicator::UnExclude() {
    Tracker::Get()->Lock();
    Tracker::Get()->SendStr(std::string("unexclude"));
    Tracker::Get()->SendStr(name());
    std::string unlock_token;
    Tracker::Get()->RecvStr(unlock_token);
    CHECK_EQ(unlock_token, "unexclude_done");
    Tracker::Get()->UnLock();
}

void Communicator::BuildTopology(const int32_t& world_size) {
    auto link_map = GetLinkMap(world_size);
    auto tree_map = std::get<0>(link_map);
    auto parent_map = std::get<1>(link_map);
    auto ring_map = std::get<2>(link_map);
    auto&& rank = GetRank();
    parent_rank_ = parent_map[rank];
    VLOG_F(2, "parent rank %d", parent_rank_);
    auto neighbors = tree_map[rank];
    num_neighbors_ = neighbors.size();
    VLOG_F(2, "number nerighbors %d", num_neighbors_);
    for (int i = 0; i < num_neighbors_; ++i) {
        int nrank = neighbors[i];
        // tracker_->RecvInt(nrank);
        VLOG_F(2, "neighbor %d", nrank);
        tree_neighbors_[nrank] = 1;
    }
    auto ring = ring_map[rank];
    prev_rank_ = ring.first;
    VLOG_F(2, "previous rank %d", prev_rank_);
    next_rank_ = ring.second;
    VLOG_F(2, "next rank %d", next_rank_);
    // get the global tree map
    std::vector<int> nodes(world_size);
    std::vector<std::pair<int, int>> edges;
    uint32_t node_index = 0;
    for (const auto& item : tree_map) {
        int from = item.first;
        nodes[node_index] = from;
        int num_neighbors = item.second.size();
        for (int j = 0; j < num_neighbors; j++) {
            int to = item.second[j];
            edges.emplace_back(std::make_pair(from, to));
        }
        node_index++;
    }
    tree_map_.Create(nodes, edges);
}
/*!
 * \brief connect to the tracker to fix the the missing links
 *   this function is also used when the comm start up
 */
void Communicator::ReConnectLinks(const std::tuple<int, int>& num_conn_accept) {
    this->Register();
    this->Exclude();
    int num_conn = 0, num_accept = 0;
    std::tie(num_conn, num_accept) = num_conn_accept;
    for (auto& peer_addr : Tracker::Get()->peer_addrs()) {
        int hrank = peer_addr.first;
        auto haddr = peer_addr.second;
        std::shared_ptr<IChannel> channel;
#ifdef RDC_USE_RDMA
        if (GetAdapter()->backend() == kRdma) {
            channel.reset(new RdmaChannel);
        } else {
            channel.reset(new TcpChannel);
        }
#else
        channel.reset(new TcpChannel);
#endif
        if (channel->Connect(haddr) != true) {
            channel->Close();
            LOG_F(ERROR, "Connect Error");
            continue;
        } else {
            int hrank = 0;
            CHECK_F(channel->RecvInt(hrank) == WorkStatus::kFinished,
                    "Reconnect Link failure 14");
            channel->SendInt(GetRank());
        }
        all_links_[hrank] = channel;
    }
    // listen to incoming links
    for (int i = 0; i < num_accept; ++i) {
        IChannel* channel = GetAdapter()->Accept();
        std::shared_ptr<IChannel> schannel(channel);
        int hrank = 0;
        channel->SendInt(GetRank());
        CHECK_F(channel->RecvInt(hrank) == WorkStatus::kFinished,
                "ReConnect Link failure 15");
        all_links_[hrank] = schannel;
    }
    CHECK_EQ(all_links_.size(), GetWorldSize() - 1);
    // setup tree links and ring structure
    tree_links.clear();
    for (auto& link_with_rank : all_links_) {
        // set the socket to non-blocking mode, enable TCP keepalive
        auto cur_rank = link_with_rank.first;
        auto cur_link = link_with_rank.second;
        // post check
        if (cur_link->CheckError()) {
            LOG_F(ERROR, "[%d] Detected error from [%d]", GetRank(), cur_rank);
        }
        if (tree_neighbors_.count(cur_rank) != 0) {
            tree_links.push_back(cur_link.get());
        }
        if (cur_rank == prev_rank_) {
            ring_prev_ = cur_link.get();
        }
        if (cur_rank == next_rank_) {
            ring_next_ = cur_link.get();
        }
    }
    CHECK_F(prev_rank_ == -1 || ring_prev_ != nullptr,
            "cannot find prev link in the ring");
    CHECK_F(next_rank_ == -1 || ring_next_ != nullptr,
            "cannot find next link in the ring");
    Tracker::Get()->TrackerPrint("Connected done");
    this->UnExclude();
}

//std::unique_ptr<ICommunicator> Communicator::CreateGroup(
//    const std::vector<int>& groups, const std::string& name) {
//    return utils::make_unique<Communicator>();
//}
void Communicator::Send(Buffer sendbuf, int dest) {
    auto wc = all_links_[dest]->ISend(sendbuf);
    wc->Wait();
    return;
}
void Communicator::Recv(Buffer recvbuf, int src) {
    auto wc = all_links_[src]->IRecv(recvbuf);
    wc->Wait();
    return;
}

WorkCompletion* Communicator::ISend(Buffer sendbuf, int dest) {
    return all_links_[dest]->ISend(sendbuf);
}

WorkCompletion* Communicator::IRecv(Buffer recvbuf, int src) {
    return all_links_[src]->IRecv(recvbuf);
}

}  // namespace comm
}  // namespace rdc
