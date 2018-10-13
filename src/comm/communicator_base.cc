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
#include "core/env.h"
#include "core/logging.h"
#include "core/threadpool.h"
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
    tracker_uri_ = "NULL";
    tracker_port_ = 9000;
    tracker_connected_ = false;
    tracker_closed_ = false;
    tracker_lock_ = std::make_shared<std::mutex>();
    host_uri_ = "";
    worker_port_ = 9910;
    rank_ = -1;
    world_size_ = -1;
    connect_retry_ = 5;
    version_number = 0;
    // 32 K items
    reduce_ring_mincount_ = 1;
    // reduce_ring_mincount_ = 1 << 15;
    // tracker URL
    err_link = nullptr;
    child_counter_ = 0;
    this->SetParam("rdc_reduce_buffer", "256MB");
    // setup possible enviroment variable of intrest
    env_vars_.push_back("rdc_reduce_buffer");
    env_vars_.push_back("rdc_reduce_ring_mincount");
    // also include dmlc support direct variables
    env_vars_.push_back("RDC_NUM_ATTEMPT");
    env_vars_.push_back("RDC_TRACKER_URI");
    env_vars_.push_back("RDC_TRACKER_PORT");
    env_vars_.push_back("WORKER_CONNECT_RETRY");
}
Communicator::Communicator() : Communicator(kWorldCommName) {}
Communicator::Communicator(const Communicator& other) {
    world_size_ = other.world_size_;
    rank_ = other.rank_;
    tracker_ = other.tracker_;
    tracker_lock_ = other.tracker_lock_;
    worker_port_ = other.worker_port_;
    peer_addrs_ = other.peer_addrs_;
    num_neighbors_ = other.num_neighbors_;
    tree_neighbors_ = other.tree_neighbors_;
    parent_rank_ = other.parent_rank_;
    tree_map_ = other.tree_map_;
    num_conn_ = other.num_conn_;
    num_accept_ = other.num_accept_;
    prev_rank_ = other.prev_rank_;
    next_rank_ = other.next_rank_;
}
// initialization function
void Communicator::Init(int argc, char* argv[]) {
    // init logging
    // logging::init(argc, argv);
    const std::string& thread_name = std::string("comm:") + name_;
    logging::set_thread_name(thread_name.c_str());
    // setup from enviroment variables
    // handler to get variables from env
    for (size_t i = 0; i < env_vars_.size(); ++i) {
        const char* value = Env::Get()->Find(env_vars_[i].c_str());
        if (value != nullptr) {
            LOG_F(INFO, "%s %s", env_vars_[i].c_str(), value);
            this->SetParam(env_vars_[i].c_str(), value);
        }
    }
    // pass in arguments override env variable.
    for (int i = 0; i < argc; ++i) {
        char name[256], val[256];
        if (sscanf(argv[i], "%[^=]=%s", name, val) == 2) {
            this->SetParam(name, val);
        }
    }

    // clear the setting before start reconnection
    this->rank_ = -1;
    //---------------------
    // start
    CHECK_F(all_links_.size() == 0, "can only call Init once");
    std::string interface, ip;
    network::GetAvailableInterfaceAndIP(&interface, &ip);
    worker_port_ = network::GetAvailablePort();
    this->host_uri_ = ip;
    std::tie(num_conn_, num_accept_) = this->ConnectTracker();
    this->BuildTopology(world_size_);
    // get information from tracker
    //    conn_lock_.lock();
    this->ReConnectLinks(std::make_tuple(num_conn_, num_accept_));
    //    conn_lock_.unlock();
}

void Communicator::NewCommunicator(const std::string& name) {
    // increase volumn of threadpool
    if (GetAdapter()->backend() == kTcp) {
        ThreadPool::Get()->AddWorkers(Env::Get()->GetEnv("RDC_NUM_WORKERS", 0));
    }
    std::unique_lock<std::mutex> comm_lock(comm_lock_);
    if (name == kWorldCommName) return;
    if (sub_comms_.count(name)) return;
    comm_lock.unlock();
    auto comm = utils::make_unique<Communicator>(*this);
    comm->set_name(name);
    std::unique_lock<std::mutex> lock(*tracker_lock_);
    tracker_cond_.wait(lock, [this] { return tracker_connected_; });
    // connection in current communicator
    lock.unlock();
    //    conn_lock_.lock();
    this->BuildTopology(world_size_);
    comm->ReConnectLinks(std::make_tuple(num_conn_, num_accept_));
    //    conn_lock_.unlock();
    // add this communicator to the goverment of main communicator
    comm_lock.lock();
    this->sub_comms_[name] = std::move(comm);
}
// register communicator to tracker
void Communicator::Register() {
    if (tracker_uri_ == "NULL") return;
    tracker_lock_->lock();
    tracker_->SendStr(std::string("register"));
    tracker_->SendStr(name_);
    tracker_lock_->unlock();
}
void Communicator::Shutdown() {
    if (tracker_uri_ == "NULL") return;
    // notify tracker rank i have shutdown
    this->Barrier();
    tracker_lock_->lock();
    tracker_->SendStr(std::string("shutdown"));
    if (!tracker_closed_) {
        tracker_->Close();
    }
    tracker_lock_->unlock();
    //    TcpAdapter::Get()->Shutdown();
}
void Communicator::TrackerPrint(const std::string& msg) {
    if (tracker_uri_ == "NULL") {
        LOG_F(INFO, "@node[%d] %s", rank_, msg.c_str());
        return;
    }
    tracker_lock_->lock();
    tracker_->SendStr(std::string("print"));
    tracker_->SendStr(msg);
    tracker_lock_->unlock();
}
void Communicator::Barrier() {
    this->Exclude();
    tracker_lock_->lock();
    tracker_->SendStr(std::string("barrier"));
    tracker_->SendStr(name_);
    std::string barrier_token;
    tracker_->RecvStr(barrier_token);
    CHECK_EQ(barrier_token, "barrier_done");
    tracker_lock_->unlock();
    this->UnExclude();
}
void Communicator::Exclude() {
    std::string lock_token;
    do {
        tracker_lock_->lock();
        tracker_->SendStr(std::string("exclude"));
        tracker_->SendStr(name_);
        tracker_->RecvStr(lock_token);
        tracker_lock_->unlock();
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    } while (lock_token != "exclude_done");
}
void Communicator::UnExclude() {
    tracker_lock_->lock();
    tracker_->SendStr(std::string("unexclude"));
    tracker_->SendStr(name_);
    std::string unlock_token;
    tracker_->RecvStr(unlock_token);
    CHECK_EQ(unlock_token, "unexclude_done");
    tracker_lock_->unlock();
}

// util to parse data with unit suffix
inline size_t ParseUnit(const char* name, const char* val) {
    char unit;
    unsigned long amt;  // NOLINT(*)
    int n = sscanf(val, "%lu%c", &amt, &unit);
    size_t amount = amt;
    if (n == 2) {
        switch (unit) {
            case 'B':
                return amount;
            case 'K':
                return amount << 10UL;
            case 'M':
                return amount << 20UL;
            case 'G':
                return amount << 30UL;
            default:
                LOG_F(ERROR, "invalid format for %s", name);
                return 0;
        }
    } else if (n == 1) {
        return amount;
    } else {
        LOG_F(ERROR,
              "invalid format for %s,"
              "shhould be {integer}{unit}, unit can be {B, KB, MB, GB}",
              name);
        return 0;
    }
}
void Communicator::SetParam(const char* name, const char* val) {
    if (!strcmp(name, "RDC_TRACKER_URI")) tracker_uri_ = val;
    if (!strcmp(name, "RDC_TRACKER_PORT")) tracker_port_ = atoi(val);
    if (!strcmp(name, "rdc_world_size")) world_size_ = atoi(val);
    if (!strcmp(name, "rdc_reduce_ring_mincount")) {
        reduce_ring_mincount_ = ParseUnit(name, val);
    }
    if (!strcmp(name, "RDC_WORKER_CONNECT_RETRY")) {
        connect_retry_ = atoi(val);
    }
}
void Communicator::BuildTopology(const int32_t& world_size) {
    auto link_map = GetLinkMap(world_size);
    auto tree_map = std::get<0>(link_map);
    auto parent_map = std::get<1>(link_map);
    auto ring_map = std::get<2>(link_map);
    parent_rank_ = parent_map[rank_];
    VLOG_F(2, "parent rank %d", parent_rank_);
    auto neighbors = tree_map[rank_];
    num_neighbors_ = neighbors.size();
    VLOG_F(2, "number nerighbors %d", num_neighbors_);
    for (int i = 0; i < num_neighbors_; ++i) {
        int nrank = neighbors[i];
        // tracker_->RecvInt(nrank);
        VLOG_F(2, "neighbor %d", nrank);
        tree_neighbors_[nrank] = 1;
    }
    auto ring = ring_map[rank_];
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
 * \brief initialize connection to the tracker
 * \return a socket that initializes the connection
 */
std::tuple<int, int> Communicator::ConnectTracker(const char* cmd) {
    // get information from tracker
    tracker_lock_->lock();
    if (!tracker_connected_) {
        tracker_ = std::make_shared<TcpSocket>();
        int retry = 0;
        do {
            if (!tracker_->Connect(tracker_uri_.c_str(), tracker_port_)) {
                if (++retry >= connect_retry_) {
                    LOG_F(ERROR, "connect to (failed): [%s]\n",
                          tracker_uri_.c_str());
                    LOG_F(ERROR, "Connect");
                } else {
                    LOG_F(ERROR, "retry connect to ip(retry time %d): [%s]\n",
                          retry, tracker_uri_.c_str());
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }
            break;
        } while (true);
        tracker_connected_ = true;
        // single node mode
        if (tracker_uri_ == "NULL") {
            rank_ = 0;
            world_size_ = 1;
            return std::make_tuple(-1, -1);
        }
        // start listener at very begining
        GetAdapter()->Listen(worker_port_);
        tracker_->SendStr(std::string(cmd));
        rank_ = Env::Get()->GetEnv("RDC_RANK", -1);
        // first send my rank to tracker for global rank scheduling
        tracker_->SendInt(rank_);
        // send my addr to tracker for decision making
        auto backend_str = GetAdapter()->backend_str();
        auto host_addr = str_utils::SPrintf("%s:%s:%d", backend_str.c_str(),
                                            host_uri_.c_str(), worker_port_);
        tracker_->SendStr(host_addr);

        tracker_->RecvInt(world_size_);
        VLOG_F(2, "workd size %d", world_size_);
        // recieve my new rank from tracker
        tracker_->RecvInt(rank_);
        VLOG_F(2, "new rank %d", rank_);
        logging::add_file(str_utils::SPrintf("log/%d", rank_).c_str(),
                          logging::Truncate, logging::Verbosity_MAX);
        logging::g_stderr_verbosity = 1;
        // get number of to connect and number of to accept nodes from tracker
        tracker_->RecvInt(num_conn_);

        VLOG_F(2, "number peers need to connect %d", num_conn_);
        tracker_->RecvInt(num_accept_);

        VLOG_F(2, "number peers need to accept %d", num_accept_);
        for (int i = 0; i < num_conn_; ++i) {
            std::string haddr;
            int hrank = -1;
            tracker_->RecvStr(haddr);
            tracker_->RecvInt(hrank);
            peer_addrs_[hrank] = haddr;
        }
    }
    tracker_lock_->unlock();
    tracker_cond_.notify_all();
    return std::tie(num_conn_, num_accept_);
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
    for (auto& peer_addr : peer_addrs_) {
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
            CHECK_F(channel->RecvInt(hrank) == true,
                    "Reconnect Link failure 14");
            channel->SendInt(rank_);
        }
        all_links_[hrank] = channel;
    }
    // listen to incoming links
    for (int i = 0; i < num_accept; ++i) {
        IChannel* channel = GetAdapter()->Accept();
        std::shared_ptr<IChannel> schannel(channel);
        int hrank = 0;
        channel->SendInt(rank_);
        CHECK_F(channel->RecvInt(hrank) == true, "ReConnect Link failure 15");
        all_links_[hrank] = schannel;
    }
    CHECK_EQ(all_links_.size(), world_size_ - 1);
    // setup tree links and ring structure
    tree_links.clear();
    for (auto& link_with_rank : all_links_) {
        // set the socket to non-blocking mode, enable TCP keepalive
        auto cur_rank = link_with_rank.first;
        auto cur_link = link_with_rank.second;
        // post check
        if (cur_link->CheckError()) {
            LOG_F(ERROR, "[%d] Detected error from [%d]", rank_, cur_rank);
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
    this->TrackerPrint("Connected done");
    this->UnExclude();
}
void Communicator::TryAllreduce(Buffer sendrecvbuf, ReduceFunction reducer) {
    if (sendrecvbuf.size_in_bytes() > reduce_ring_mincount_) {
        return this->TryAllreduceRing(sendrecvbuf, reducer);
    } else {
        return this->TryAllreduceTree(sendrecvbuf, reducer);
    }
}
void Communicator::TryReduceTree(Buffer sendrecvbuf, Buffer reducebuf,
                                 ReduceFunction reducer, int root) {
    auto dists_from_root = tree_map_.ShortestDist(root);
    auto dist_from_root = dists_from_root[rank_];
    auto neighbors = tree_map_.GetNeighbors(rank_);
    std::unordered_set<int> recv_from_nodes;
    int send_to_node = -1;
    for (const auto& neighbor : neighbors) {
        if (dists_from_root[neighbor] == dist_from_root + 1) {
            recv_from_nodes.insert(neighbor);
        } else if (dists_from_root[neighbor] == dist_from_root - 1) {
            send_to_node = neighbor;
        }
    }
    ChainWorkCompletion wc;
    for (const auto& recv_from_node : recv_from_nodes) {
        wc << all_links_[recv_from_node]->IRecv(reducebuf);
        reducer(reducebuf, sendrecvbuf);
    }

    if (send_to_node != -1) {
        wc << all_links_[send_to_node]->ISend(sendrecvbuf);
    }
    wc.Wait();

    return;
}
void Communicator::TryBroadcast(Buffer sendrecvbuf, int root) {
    auto dists_from_root = tree_map_.ShortestDist(root);
    auto dist_from_root = dists_from_root[rank_];
    auto neighbors = tree_map_.GetNeighbors(rank_);
    std::unordered_set<int> send_to_nodes;
    int recv_from_node = -1;
    for (const auto& neighbor : neighbors) {
        if (dists_from_root[neighbor] == dist_from_root + 1) {
            send_to_nodes.insert(neighbor);
        } else if (dists_from_root[neighbor] == dist_from_root - 1) {
            recv_from_node = neighbor;
        }
    }
    ChainWorkCompletion wc;
    if (recv_from_node != -1) {
        wc << all_links_[recv_from_node]->IRecv(sendrecvbuf);
    }
    for (const auto& send_to_node : send_to_nodes) {
        wc << all_links_[send_to_node]->ISend(sendrecvbuf);
    }
    wc.Wait();
    return;
}

void Communicator::TryAllreduceTree(Buffer sendrecvbuf,
                                    ReduceFunction reducer) {
    Buffer reducebuf(sendrecvbuf.size_in_bytes());
    reducebuf.AllocTemp(utils::AllocTemp);
    TryReduceTree(sendrecvbuf, reducebuf, reducer, 0);
    reducebuf.FreeTemp(utils::Free);
    TryBroadcast(sendrecvbuf, 0);
}
void Communicator::TryAllgatherRing(std::vector<Buffer> sendrecvbufs) {
    // read from next link and send to prev one
    auto &prev = ring_prev_, &next = ring_next_;
    // need to reply on special rank structure
    CHECK_F(next_rank_ == (rank_ + 1) % world_size_ &&
                rank_ == (prev_rank_ + 1) % world_size_,
            "need to assume rank structure");
    const size_t count_bufs = world_size_;
    const size_t stop_write_idx = count_bufs + rank_ - 1;
    const size_t stop_read_idx = count_bufs + rank_;
    size_t write_idx = rank_;
    size_t read_idx = rank_ + 1;
    while (true) {
        bool finished = true;
        if (read_idx != stop_read_idx) {
            finished = false;
        }
        if (write_idx != stop_write_idx) {
            finished = false;
        }
        if (finished) break;
        ChainWorkCompletion wc;
        if (write_idx < read_idx && write_idx != stop_write_idx) {
            size_t start = write_idx % count_bufs;
            wc << prev->ISend(sendrecvbufs[start]);
            write_idx++;
        }
        if (read_idx != stop_read_idx) {
            size_t start = read_idx % count_bufs;
            wc << next->IRecv(sendrecvbufs[start]);
            //            wc.Wait();
            read_idx++;
        }
        wc.Wait();
    }
}
void Communicator::TryReduceScatterRing(Buffer sendrecvbuf, Buffer reducebuf,
                                        ReduceFunction reducer) {
    // read from next link and send to prev one
    auto &prev = ring_prev_, &next = ring_next_;
    // need to reply on special rank structure
    CHECK_F(next_rank_ == (rank_ + 1) % world_size_ &&
                rank_ == (prev_rank_ + 1) % world_size_,
            "need to assume rank structure");
    uint64_t n = static_cast<uint64_t>(world_size_);
    const auto& ranges = utils::Split(0, sendrecvbuf.Count(), n);
    uint64_t write_idx = next_rank_;
    uint64_t read_idx = next_rank_ + 1;
    uint64_t reduce_idx = read_idx;
    // position to stop reading
    const uint64_t stop_read_idx = n + next_rank_;
    // position to stop writing
    size_t stop_write_idx = n + rank_;
    const auto& type_nbytes = sendrecvbuf.type_nbytes();
    if (stop_write_idx > stop_read_idx) {
        stop_write_idx -= n;
        CHECK_F(write_idx <= stop_write_idx, "write ptr boundary check");
    }
    while (true) {
        bool finished = true;
        if (read_idx != stop_read_idx) {
            finished = false;
        }
        if (write_idx != stop_write_idx) {
            finished = false;
        }
        if (finished) break;
        ChainWorkCompletion wc;
        if (write_idx < reduce_idx && write_idx != stop_write_idx) {
            uint64_t write_pos = write_idx % n;
            uint64_t write_size =
                (ranges[write_pos].second - ranges[write_pos].first) *
                type_nbytes;
            uint64_t write_start = ranges[write_pos].first * type_nbytes;
            wc << prev->ISend(
                sendrecvbuf.Slice(write_start, write_start + write_size));
            write_idx++;
        }
        if (read_idx != stop_read_idx) {
            uint64_t read_pos = read_idx % n;
            uint64_t read_start = ranges[read_pos].first * type_nbytes;
            uint64_t read_size =
                (ranges[read_pos].second - ranges[read_pos].first) *
                type_nbytes;
            wc << next->IRecv(
                reducebuf.Slice(read_start, read_start + read_size));
            wc.Wait();
            CHECK_F(read_idx <= stop_read_idx, "[%d] read_ptr boundary check",
                    rank_);
            read_idx++;
            size_t reduce_pos = reduce_idx % n;
            size_t reduce_start = ranges[reduce_pos].first * type_nbytes;
            size_t reduce_size =
                (ranges[reduce_pos].second - ranges[reduce_pos].first) *
                type_nbytes;
            reducer(
                reducebuf.Slice(reduce_start, reduce_start + reduce_size),
                sendrecvbuf.Slice(reduce_start, reduce_start + reduce_size));
            reduce_idx++;
        }
    }
    return;
}
void Communicator::TryAllreduceRing(Buffer sendrecvbuf,
                                    ReduceFunction reducer) {
    Buffer reducebuf(sendrecvbuf.size_in_bytes());
    reducebuf.AllocTemp(utils::AllocTemp);
    reducebuf.set_type_nbytes(sendrecvbuf.type_nbytes());
    TryReduceScatterRing(sendrecvbuf, reducebuf, reducer);
    reducebuf.FreeTemp(utils::Free);
    uint64_t n = static_cast<uint64_t>(world_size_);
    const auto& ranges = utils::Split(0, sendrecvbuf.Count(), n);
    // get rank of previous
    std::vector<Buffer> sendrecvbufs(n);
    for (auto i = 0U; i < n; i++) {
        uint64_t begin = ranges[i].first;
        uint64_t end = ranges[i].second;
        uint64_t size = (end - begin) * sendrecvbuf.type_nbytes();
        sendrecvbufs[i].set_size_in_bytes(size);
        sendrecvbufs[i].set_addr(utils::IncrVoidPtr(
            sendrecvbuf.addr(), begin * sendrecvbuf.type_nbytes()));
    }
    return TryAllgatherRing(sendrecvbufs);
}

std::unique_ptr<ICommunicator> Communicator::CreateGroup(
    const std::vector<int>& groups, const std::string& name) {
    return utils::make_unique<Communicator>();
}
void Communicator::Send(Buffer sendbuf, int dest) {
    auto wc = all_links_[dest]->ISend(sendbuf);
    wc.Wait();
    return;
}
void Communicator::Recv(Buffer recvbuf, int src) {
    auto wc = all_links_[src]->IRecv(recvbuf);
    wc.Wait();
    return;
}

WorkCompletion Communicator::ISend(Buffer sendbuf, int dest) {
    return all_links_[dest]->ISend(sendbuf);
}

WorkCompletion Communicator::IRecv(Buffer recvbuf, int src) {
    return all_links_[src]->IRecv(recvbuf);
}

}  // namespace comm
}  // namespace rdc
