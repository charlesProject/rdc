/*!
 *  Copyright (c) 2018 by Contributors
 * \file allreduce_base.cc
 * \brief Basic implementation of AllReduce
 *
 * \author Ankun Zheng
 */
#include <map>
#include <cstdlib>
#include <cstring>
#include "utils/network_utils.h"
#include "utils/string_utils.h"
#include "core/threadpool.h"
#include "core/logging.h"
#include "comm/communicator_base.h"

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
    reduce_ring_mincount_ = 32 << 10;
    //reduce_ring_mincount_ = 32;
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
    //logging::init(argc, argv);
    const std::string& thread_name = std::string(
        "comm:") + name_;
    logging::set_thread_name(thread_name.c_str());
    // setup from enviroment variables
    // handler to get variables from env
    for (size_t i = 0; i < env_vars_.size(); ++i) {
        const char *value = getenv(env_vars_[i].c_str());
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
    network_utils::GetAvailableInterfaceAndIP(&interface, &ip);
    worker_port_ = network_utils::GetAvailablePort();
    this->host_uri_ = ip;
    std::tie(num_conn_, num_accept_) = this->ConnectTracker();
    // get information from tracker
    conn_lock_.lock();
    this->ReConnectLinks(std::make_tuple(num_conn_, num_accept_));
    conn_lock_.unlock();
}

void Communicator::NewCommunicator(const std::string& name) {
    // increase volumn of threadpool
    ThreadPool::Get()->AddWorkers(2 * std::atoi(getenv("RDC_NUM_WORKERS")));
    comm_lock_.lock();
    if (name == kWorldCommName) return;
    if (sub_comms_.count(name)) return;
    comm_lock_.unlock();
    auto comm = utils::make_unique<Communicator>(*this);
    comm->set_name(name);
    std::unique_lock<std::mutex> lock(*tracker_lock_);
    tracker_cond_.wait(lock, [this]{ return tracker_connected_; });
    // connection in current communicator
    lock.unlock();
    conn_lock_.lock();
    comm->ReConnectLinks(std::make_tuple(num_conn_, num_accept_));
    conn_lock_.unlock();
    // add this communicator to the goverment of main communicator
    comm_lock_.lock();
    this->sub_comms_[name] = std::move(comm);
    comm_lock_.unlock();
}
// register communicator to tracker
void Communicator::Register() {
    if (tracker_uri_ == "NULL") return;
    LOG(INFO) << name_;
    tracker_lock_->lock();
    LOG(INFO) << name_;
    tracker_->SendStr(std::string("register"));
    tracker_->SendStr(name_);
    tracker_lock_->unlock();
}
void Communicator::Shutdown() {
    if (tracker_uri_ == "NULL") return;
    // notify tracker rank i have shutdown
    tracker_lock_->lock();
    tracker_->SendStr(std::string("shutdown"));
    if (!tracker_closed_) {
        tracker_->Close();
    }
    tracker_lock_->unlock();
}
void Communicator::TrackerPrint(const std::string &msg) {
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
    tracker_lock_->lock();
    tracker_->SendStr(std::string("barrier"));
    tracker_->SendStr(name_.c_str());
    std::string barrier_token_;
    tracker_->RecvStr(barrier_token_);
    CHECK_EQ(barrier_token_, "barrier_done");
    tracker_lock_->unlock();
}
// util to parse data with unit suffix
inline size_t ParseUnit(const char *name, const char *val) {
    char unit;
    unsigned long amt;  // NOLINT(*)
    int n = sscanf(val, "%lu%c", &amt, &unit);
    size_t amount = amt;
    if (n == 2) {
        switch (unit) {
            case 'B': return amount;
            case 'K': return amount << 10UL;
            case 'M': return amount << 20UL;
            case 'G': return amount << 30UL;
            default: LOG_F(ERROR, "invalid format for %s", name); return 0;
        }
    } else if (n == 1) {
        return amount;
    } else {
        LOG_F(ERROR,"invalid format for %s,"                               \
                   "shhould be {integer}{unit}, unit can be {B, KB, MB, GB}", name);
        return 0;
    }
}
void Communicator::SetParam(const char *name, const char *val) {
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
/*!
 * \brief initialize connection to the tracker
 * \return a socket that initializes the connection
 */
std::tuple<int, int> Communicator::ConnectTracker(const char* cmd)  {
    // get information from tracker
    tracker_lock_->lock();
    if (!tracker_connected_) {
        tracker_ = std::make_shared<TcpChannel>();
        int retry = 0;
        do {
            if (tracker_->Connect(tracker_uri_.c_str(), tracker_port_)
                    != Status::kSuccess) {
                if (++retry >= connect_retry_) {
                    LOG_F(ERROR, "connect to (failed): [%s]\n",
                          tracker_uri_.c_str());
                    LOG_F(ERROR, "Connect");
                } else {
                    LOG_F(ERROR, "retry connect to ip(retry time %d): [%s]\n",
                                  retry, tracker_uri_.c_str());
                    #ifdef _MSC_VER
                    Sleep(1);
                    #else
                    sleep(1);
                    #endif
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
        TcpPoller::Get()->Listen(worker_port_);
        tracker_->SendStr(std::string(cmd));
        CHECK_F(tracker_->RecvInt(world_size_) == Status::kSuccess,
                "ReConnectLink fail to recv world size");
        LOG_F(INFO, "%d", world_size_);
        CHECK_F(tracker_->RecvInt(rank_) == Status::kSuccess,
               "ReConnectLink fail to recv rank");
        LOG_F(INFO, "%d", rank_);
        logging::add_file(str_utils::SPrintf("log/%d", rank_).c_str(),
                          logging::Truncate, logging::Verbosity_MAX);
        // send back socket listening port to tracker
        auto host_addr = str_utils::SPrintf("%s:%s:%d",
                "tcp", host_uri_.c_str(), worker_port_);
        CHECK_F(tracker_->SendStr(host_addr) == Status::kSuccess,
               "ReConnectLink fail to send my addr");
        // get new ranks
        CHECK_F(tracker_->RecvInt(parent_rank_) ==
                Status::kSuccess, "ReConnectLink fail to recv parent rank");
        LOG_F(INFO, "%d", parent_rank_);
        CHECK_F(tracker_->RecvInt(num_neighbors_) == Status::kSuccess,
                "ReConnectLink fail to recv num neighbors");
        LOG_F(INFO, "%d", num_neighbors_);
        for (int i = 0; i < num_neighbors_; ++i) {
              int nrank;
              CHECK_F(tracker_->RecvInt(nrank) == Status::kSuccess,
                      "ReConnectLink fail to recv neighbor rank");
              LOG_F(INFO, "%d", nrank);
              tree_neighbors_[nrank] = 1;
        }
        CHECK_F(tracker_->RecvInt(prev_rank_) == Status::kSuccess,
               "ReConnectLink fail to recv prev rank");
        LOG_F(INFO, "pr %d", prev_rank_);
        CHECK_F(tracker_->RecvInt(next_rank_) == Status::kSuccess,
               "ReConnectLink fail to recv next rank");

        LOG_F(INFO, "nr %d", next_rank_);
        // get the global tree map
        std::vector<int> nodes(this->world_size_);
        std::vector<std::pair<int, int>> edges;
        for (int i = 0; i < this->world_size_; i++) {
            int from = 0;
            CHECK_F(tracker_->RecvInt(from) == Status::kSuccess,
                    "ReConnectLink fail to recv from rank");
            nodes[i] = from;
            int num_neighbors = 0;
            CHECK_F(tracker_->RecvInt(num_neighbors) == Status::kSuccess,
                    "ReConnectLink fail to recv num neighbors");
            for (int j = 0; j < num_neighbors; j++) {
                int to = 0;
                CHECK_F(tracker_->RecvInt(to) == Status::kSuccess,
                    "   ReConnectLink fail to recv to rank");
                edges.emplace_back(std::make_pair(from, to));
            }
        }
        tree_map_.Create(nodes, edges);
        // get number of to connect and number of to accept nodes from tracker
        CHECK_F(tracker_->RecvInt(num_conn_) == Status::kSuccess,
               "ReConnectLink fail to recv num conn");

        LOG_F(INFO, "nc %d", num_conn_);
        CHECK_F(tracker_->RecvInt(num_accept_) ==  Status::kSuccess,
                "ReConnectLink fail to recv num accept");

        LOG_F(INFO, "na %d", num_accept_);
        for (int i = 0; i < num_conn_; ++i) {
            std::string haddr;
            int hrank = -1;
            CHECK_F(tracker_->RecvStr(haddr) == Status::kSuccess,
                    "ReConnectLink fail to recv peer hostname");
            CHECK_F(tracker_->RecvInt(hrank) == Status::kSuccess,
                    "ReConnectLink fail to recv peer rank");
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
void Communicator::ReConnectLinks(const std::tuple<int, int>&
        num_conn_accept) {
    this->Register();
    int num_conn = 0, num_accept = 0;
    std::tie(num_conn, num_accept) = num_conn_accept;
    for (auto& peer_addr : peer_addrs_) {
        int hrank = peer_addr.first;
        auto haddr = peer_addr.second;
        std::shared_ptr<IChannel> channel = std::make_shared<TcpChannel>();
        if (channel->Connect(haddr) != Status::kSuccess) {
            channel->Close();
            LOG_F(ERROR,"Error");
            continue;
        } else {
            int hrank = 0;
            CHECK_F(channel->RecvInt(hrank) == Status::kSuccess,
                    "Reconnect Link failure 14");
            channel->SendInt(rank_);
        }
        LOG_F(INFO, "PEER %s %d", name_.c_str(), hrank);
        all_links_[hrank] = channel;
    }
    LOG(INFO) << name_;
    // listen to incoming links
    for (int i = 0; i < num_accept; ++i) {
        TcpChannel* channel= TcpPoller::Get()->Accept();
        std::shared_ptr<IChannel> schannel(channel);
        int hrank = 0;
        channel->SendInt(rank_);
        CHECK_F(channel->RecvInt(hrank) == Status::kSuccess,
                "ReConnect Link failure 15");
        LOG_F(INFO, "PEER %s %d", name_.c_str(), hrank);
        all_links_[hrank] = schannel;
    }
    LOG_F(INFO, "LINKS %s %d", name_.c_str(), all_links_.size());
    std::string msg;
    for (auto& i : all_links_) {
        msg += std::to_string(i.first) + " ";
    }
    LOG_F(INFO, msg.c_str());

    // setup tree links and ring structure
    tree_links.clear();
    for (auto& link_with_rank : all_links_) {
        // set the socket to non-blocking mode, enable TCP keepalive
        auto cur_rank = link_with_rank.first;
        auto cur_link = link_with_rank.second;
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
    TrackerPrint("Connected done");
}
void Communicator::TryAllreduce(void *sendrecvbuf_,
                            size_t type_nbytes,
                            size_t count,
                            ReduceFunction reducer) {
    if (count > reduce_ring_mincount_) {
        return this->TryAllreduceRing(sendrecvbuf_, type_nbytes, count, reducer);
    }
    else {
        return this->TryAllreduceTree(sendrecvbuf_, type_nbytes, count, reducer);
    }
}
void Communicator::TryReduceTree(void* sendrecvbuf_,
                                void* reducebuf_,
                                size_t type_nbytes,
                                size_t count,
                                ReduceFunction reducer,
                                int root) {
    auto dists_from_root = tree_map_.ShortestDist(root);
    int dist_from_root = dists_from_root[rank_];
    auto neighbors = tree_map_.GetNeighbors(rank_);
    std::unordered_set<int> recv_from_nodes;
    int send_to_node = -1;
    for (const auto& neighbor : neighbors) {
        if (dists_from_root[neighbor] == dist_from_root + 1) {
            recv_from_nodes.insert(neighbor);
        } else if (dists_from_root[neighbor] == dist_from_root - 1){
            send_to_node = neighbor;
        }
    }
    int total_size = count * type_nbytes;
    char* reducebuf = reinterpret_cast<char*>(reducebuf_);
    char* sendrecvbuf = reinterpret_cast<char*>(sendrecvbuf_);

    for (const auto& recv_from_node : recv_from_nodes) {
        auto wc = all_links_[recv_from_node]->IRecv(reducebuf_, total_size);
        wc.Wait();
        reducer(reducebuf, sendrecvbuf,
                count, MPI::Datatype(type_nbytes));
    }


    if (send_to_node != -1) {
        auto wc = all_links_[send_to_node]->ISend(sendrecvbuf_, total_size);
        wc.Wait();
    }

    return;
}
void Communicator::TryBroadcast(void *sendrecvbuf_, size_t total_size, int root) {
    auto dists_from_root = tree_map_.ShortestDist(root);
    int dist_from_root = dists_from_root[rank_];
    auto neighbors = tree_map_.GetNeighbors(rank_);
    std::unordered_set<int> send_to_nodes;
    int recv_from_node = -1;
    for (const auto& neighbor : neighbors) {
        if (dists_from_root[neighbor] == dist_from_root + 1) {
            send_to_nodes.insert(neighbor);
        } else if (dists_from_root[neighbor] == dist_from_root - 1){
            recv_from_node = neighbor;
        }
    }

    if (recv_from_node != -1) {
        auto wc = all_links_[recv_from_node]->IRecv(sendrecvbuf_, total_size);
        wc.Wait();
    }
    for (const auto& send_to_node : send_to_nodes) {
        auto wc = all_links_[send_to_node]->ISend(sendrecvbuf_, total_size);
        wc.Wait();
    }
    return;
}


void Communicator::TryAllreduceTree(void *sendrecvbuf_,
                                size_t type_nbytes,
                                size_t count,
                                ReduceFunction reducer) {
    void* reducebuf_  = utils::AllocTemp(type_nbytes * count);
    TryReduceTree(sendrecvbuf_, reducebuf_, type_nbytes, count, reducer, 0);
    utils::Free(reducebuf_);
    TryBroadcast(sendrecvbuf_, type_nbytes * count, 0);
}
void Communicator::TryAllgatherRing(void** sendrecvbufs_, size_t type_nbytes,
                                     size_t* counts) {
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
        if (write_idx < read_idx && write_idx != stop_write_idx) {
            size_t start = write_idx % count_bufs;
            auto wc = prev->ISend(*(sendrecvbufs_ + start),
                                  counts[start] * type_nbytes);
            wc.Wait();
            write_idx++;
        }
        if (read_idx != stop_read_idx) {
            size_t start = read_idx % count_bufs;
            auto wc = next->IRecv(*(sendrecvbufs_ + start),
                                  counts[start] * type_nbytes);
            wc.Wait();
            read_idx++;
        }

    }
}
void Communicator::TryReduceScatterRing(void *sendrecvbuf_,
                                    void* reducebuf_,
                                    size_t type_nbytes,
                                    size_t count,
                                    ReduceFunction reducer) {
    // read from next link and send to prev one
    auto& prev = ring_prev_, &next = ring_next_;
    // need to reply on special rank structure
    CHECK_F(next_rank_ == (rank_ + 1) % world_size_ &&
           rank_ == (prev_rank_ + 1) % world_size_,
           "need to assume rank structure");
    // total size of message
    const size_t total_size = type_nbytes * count;
    size_t n = static_cast<size_t>(world_size_);
    const auto& ranges = utils::Split(0, count, n);
    size_t write_idx = next_rank_;
    size_t read_idx = next_rank_ + 1;
    size_t reduce_idx = read_idx;
    // send recv buffer
    char* sendrecvbuf = reinterpret_cast<char*>(sendrecvbuf_);
    // reduce buffer
    char* reducebuf = reinterpret_cast<char*>(reducebuf_);
    // position to stop reading
    const size_t stop_read_idx = n + next_rank_;
    // position to stop writing
    size_t stop_write_idx = n + rank_;
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
        if (write_idx < reduce_idx && write_idx != stop_write_idx) {
            size_t write_pos = write_idx % n;
            size_t write_size = (ranges[write_pos].second -
                                 ranges[write_pos].first) * type_nbytes;
            size_t write_start = ranges[write_pos].first * type_nbytes;
            auto wc = prev->ISend(sendrecvbuf + write_start,
                                          write_size);
            wc.Wait();
            write_idx ++;
        }
        if (read_idx != stop_read_idx) {
            size_t read_pos = read_idx % n;
            size_t read_start = ranges[read_pos].first * type_nbytes;
            size_t read_size = (ranges[read_pos].second -
                                ranges[read_pos].first) * type_nbytes;
            auto wc = next->IRecv(reducebuf + read_start, read_size);
            wc.Wait();
            CHECK_F(read_idx <= stop_read_idx,"[%d] read_ptr boundary check",
                     rank_);
            read_idx++;
            size_t reduce_pos = reduce_idx % n;
            size_t reduce_start = ranges[reduce_pos].first * type_nbytes;
            size_t reduce_size = (ranges[reduce_pos].second -
                                  ranges[reduce_pos].first) * type_nbytes;
            reducer(reducebuf + reduce_start,
                    sendrecvbuf + reduce_start,
                    static_cast<int>(reduce_size / type_nbytes),
                    MPI::Datatype(type_nbytes));
            reduce_idx++;
        }
    }
    return;
}
void Communicator::TryAllreduceRing(void *sendrecvbuf_,
                                size_t type_nbytes,
                                size_t count,
                                ReduceFunction reducer) {
    void* reducebuf = utils::AllocTemp(count * type_nbytes);
    TryReduceScatterRing(sendrecvbuf_, reducebuf, type_nbytes, 
                         count, reducer);
    utils::Free(reducebuf);
    size_t n = static_cast<size_t>(world_size_);
    const auto& ranges = utils::Split(0, count, n);
    // get rank of previous
    std::vector<void*> sendrecv_bufs(n);
    std::vector<size_t> sizes(n);
    for (int i = 0; i < n; i++) {
        size_t begin = ranges[i].first;
        size_t end = ranges[i].second;
        size_t size = end - begin;
        sizes[i] = size;
        sendrecv_bufs[i] = utils::IncrVoidPtr(sendrecvbuf_, begin * type_nbytes);
    }
    return TryAllgatherRing(utils::BeginPtr(sendrecv_bufs), type_nbytes, 
                            utils::BeginPtr(sizes));
}

std::unique_ptr<ICommunicator> Communicator::CreateGroup(
        const std::vector<int>& groups,
        const std::string& name) {
    return utils::make_unique<Communicator>();
}
void Communicator::Send(void* sendbuf_, size_t nbytes, int dest) {
    auto wc = all_links_[dest]->ISend(sendbuf_, nbytes);
    wc.Wait();
    //return wc.status();
}
void Communicator::Recv(void* recvbuf_, size_t nbytes, int src)  {
    auto wc= all_links_[src]->IRecv(recvbuf_, nbytes);
    wc.Wait();
    //return wc.status();
}
}  // namespace comm
}  // namespace rdc
