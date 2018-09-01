/*!
 *  Copyright (c) 2014 by Contributors
 * \file allreduce_base.cc
 * \brief Basic implementation of AllReduce
 *
 * \author Ankun Zheng
 */
#include <map>
#include <cstdlib>
#include <cstring>
#include "utils/network_utils.h"
#include "core/logging.h"
#include "engine/allreduce_base.h"

static const std::string kTerminalStr = "_";
namespace rabit {
namespace engine {
// constructor
AllreduceBase::AllreduceBase(void) {
    tracker_uri = "NULL";
    tracker_port = 9000;
    host_uri = "";
    slave_port = 9910;
    nport_trial = 1000;
    rank = -1;
    world_size = -1;
    connect_retry = 5;
    version_number = 0;
    // 32 K items
    //reduce_ring_mincount = 32 << 10;
    reduce_ring_mincount = 1;
    // tracker URL
    err_link = NULL;
    this->SetParam("rabit_reduce_buffer", "256MB");
    // setup possible enviroment variable of intrest
    env_vars.push_back("rabit_num_trial");
    env_vars.push_back("rabit_reduce_buffer");
    env_vars.push_back("rabit_reduce_ring_mincount");
    // also include dmlc support direct variables
    env_vars.push_back("DMLC_NUM_ATTEMPT");
    env_vars.push_back("TRACKER_URI");
    env_vars.push_back("TRACKER_PORT");
    env_vars.push_back("WORKER_CONNECT_RETRY");
}

// initialization function
void AllreduceBase::Init(int argc, char* argv[]) {
    // init logging
    //logging::init(argc, argv);
    logging::set_thread_name("main");
    // setup from enviroment variables
    // handler to get variables from env
    for (size_t i = 0; i < env_vars.size(); ++i) {
        const char *value = getenv(env_vars[i].c_str());
        if (value != nullptr) {
            this->SetParam(env_vars[i].c_str(), value);
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
    this->rank = -1;
    //---------------------
    // start
    CHECK_F(all_links.size() == 0, "can only call Init once");
    std::string interface, ip;
    network_utils::GetAvailableInterfaceAndIP(&interface, &ip);
    slave_port = network_utils::GetAvailablePort();
    this->host_uri = ip;
    this->ConnectTracker();
    // get information from tracker
    this->ReConnectLinks();
}

void AllreduceBase::Shutdown(void) {
    for (size_t i = 0; i < all_links.size(); ++i) {
        all_links[i].channel->Close();
    }
    all_links.clear();
    tree_links.clear();

    if (tracker_uri == "NULL") return;
    // notify tracker rank i have shutdown
    tracker->SendStr(std::string("shutdown"));
    tracker->Close();
}
void AllreduceBase::TrackerPrint(const std::string &msg) {
    if (tracker_uri == "NULL") {
        LOG_F(INFO, "@node[%d] %s", rank, msg.c_str()); 
        return;
    }
    tracker->SendStr(std::string("print"));
    tracker->SendStr(kTerminalStr);
    tracker->SendStr(msg);
    tracker->SendStr(kTerminalStr);
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
void AllreduceBase::SetParam(const char *name, const char *val) {
    if (!strcmp(name, "TRACKER_URI")) tracker_uri = val;
    if (!strcmp(name, "TRACKER_PORT")) tracker_port = atoi(val);
    if (!strcmp(name, "rabit_world_size")) world_size = atoi(val);
    if (!strcmp(name, "rabit_reduce_ring_mincount")) {
        reduce_ring_mincount = ParseUnit(name, val);
    }
    if (!strcmp(name, "rabit_reduce_buffer")) {
        reduce_buffer_size = (ParseUnit(name, val) + 7) >> 3;
    }
    if (!strcmp(name, "DMLC_WORKER_CONNECT_RETRY")) {
        connect_retry = atoi(val);
    }
}
/*!
 * \brief initialize connection to the tracker
 * \return a socket that initializes the connection
 */
void AllreduceBase::ConnectTracker()  {
    // get information from tracker
    tracker = utils::make_unique<TcpChannel>();
    int retry = 0;
    do {
        if (tracker->Connect(tracker_uri.c_str(), tracker_port)
                != Status::kSuccess) {
            if (++retry >= connect_retry) {
                LOG_F(ERROR, "connect to (failed): [%s]\n",
                      tracker_uri.c_str());
                LOG_F(ERROR, "Connect");
            } else {
                LOG_F(ERROR, "retry connect to ip(retry time %d): [%s]\n",
                              retry, tracker_uri.c_str());
                #ifdef _MSC_VER
                Sleep(1);
                #else
                sleep(1);
                #endif
                continue;
            }
        }
        break;
    } while (1);

    return;
}
/*!
 * \brief connect to the tracker to fix the the missing links
 *   this function is also used when the engine start up
 */
void AllreduceBase::ReConnectLinks(const char *cmd) {
    // single node mode
    if (tracker_uri == "NULL") {
      rank = 0; world_size = 1; return;
    }
    tracker->SendStr(std::string(cmd));
    tracker->SendStr(kTerminalStr);
    CHECK_F(tracker->RecvInt(rank) == Status::kSuccess,
           "ReConnectLink failure 3");
    CHECK_F(tracker->RecvInt(world_size) == Status::kSuccess,
            "ReConnectLink failure 3");
    TcpPoller::Get()->Listen(slave_port);
    // send back socket listening port to tracker
    CHECK_F(tracker->SendStr(host_uri) == Status::kSuccess,
           "ReConnectLink failure 14");
    CHECK_F(tracker->SendInt(slave_port) == Status::kSuccess,
           "ReConnectLink failure 14");
    tracker->SendStr(kTerminalStr);
    // the rank of previous link, next link in ring
    int prev_rank, next_rank;
    // the rank of neighbors
    std::map<int, int> tree_neighbors;
    // get new ranks
    int num_neighbors;
    CHECK_F(tracker->RecvInt(parent_rank) ==
            Status::kSuccess, "ReConnectLink failure 4");
    CHECK_F(tracker->RecvInt(num_neighbors) == Status::kSuccess, 
            "ReConnectLink failure 4");
    for (int i = 0; i < num_neighbors; ++i) {
          int nrank;
          CHECK_F(tracker->RecvInt(nrank) == Status::kSuccess,
                  "ReConnectLink failure 4");
          tree_neighbors[nrank] = 1;
    }
    CHECK_F(tracker->RecvInt(prev_rank) == Status::kSuccess,
           "ReConnectLink failure 4");
    CHECK_F(tracker->RecvInt(next_rank) == Status::kSuccess,
           "ReConnectLink failure 4");
    // get number of to connect and number of to accept nodes from tracker
    int num_conn, num_accept, num_error;
    do {
        CHECK_F(tracker->RecvInt(num_conn) == Status::kSuccess,
               "ReConnectLink failure 7");
        CHECK_F(tracker->RecvInt(num_accept) ==  Status::kSuccess, 
                "ReConnectLink failure 8");
        num_error = 0;
        for (int i = 0; i < num_conn; ++i) {
            LinkRecord r;
            r.channel = utils::make_unique<TcpChannel>();
            int hport, hrank;
            std::string hname;
            tracker->RecvStr(hname);
            CHECK_F(tracker->RecvInt(hport) == Status::kSuccess,
                    "ReConnectLink failure 9");
            CHECK_F(tracker->RecvInt(hrank) == Status::kSuccess,
                    "ReConnectLink failure 10");
            if (r.channel->Connect(hname.c_str(), hport) != Status::kSuccess) {
                LOG_S(ERROR);
                num_error += 1;
                r.channel->Close();
                continue;
            } else {
                int hrank = 0;
                CHECK_F(r.channel->RecvInt(hrank) == Status::kSuccess,
                        "Reconnect Link failure 10");
                r.channel->SendInt(rank);
                r.rank = hrank;
            }
            all_links[hrank] = std::move(r);
        }
    } while (num_error != 0);
    // listen to incoming links
    for (int i = 0; i < num_accept; ++i) {
        LinkRecord r;
        r.channel= TcpPoller::Get()->Accept();
        int hrank = 0;
        r.channel->SendInt(rank);
        CHECK_F(r.channel->RecvInt(hrank) == Status::kSuccess,
                "ReConnect Link failure 11");
        r.rank = hrank;
        all_links[hrank] = std::move(r);
    }
    // setup tree links and ring structure
    tree_links.clear();
    for (auto&& link_with_rank : all_links) {
        // set the socket to non-blocking mode, enable TCP keepalive
        auto cur_rank = link_with_rank.first;
        auto&& cur_link = link_with_rank.second;
        if (tree_neighbors.count(cur_rank) != 0) {
            tree_links.push_back(&cur_link);
        }
        if (cur_rank == prev_rank) {
            ring_prev = &cur_link;
        }
        if (cur_rank == next_rank) {
            ring_next = &cur_link;
        }
    }
    CHECK_F(prev_rank == -1 || ring_prev != nullptr,
           "cannot find prev ring in the link");
    CHECK_F(next_rank == -1 || ring_next != nullptr,
           "cannot find next ring in the link");
    TrackerPrint("Connected done");
}
void AllreduceBase::TryAllreduce(void *sendrecvbuf_,
                            size_t type_nbytes,
                            size_t count,
                            ReduceFunction reducer) {
    if (count > reduce_ring_mincount) {
        return this->TryAllreduceRing(sendrecvbuf_, type_nbytes, count, reducer);
    } 
//    else {
//    return this->TryAllreduceTree(sendrecvbuf_, type_nbytes, count, reducer);
//  }
}
void AllreduceBase::TryAllreduceTree(void *sendrecvbuf_,
                                size_t type_nbytes,
                                size_t count,
                                ReduceFunction reducer) {
//  auto& links = tree_links;
//  if (links.size() == 0 || count == 0) return Status::kSuccess;
//  // total size of message
//  const size_t total_size = type_nbytes * count;
//  // number of links
//  const int nlink = static_cast<int>(links.size());
//  // send recv buffer
//  char *sendrecvbuf = reinterpret_cast<char*>(sendrecvbuf_);
//  // size of space that we already performs reduce in up pass
//  size_t size_up_reduce = 0;
//  // size of space that we have already passed to parent
//  size_t size_up_out = 0;
//  // size of message we received, and send in the down pass
//  size_t size_down_in = 0;
//  // initialize the link ring-buffer and pointer
//  for (int i = 0; i < nlink; ++i) {
//      if (i != parent_index) {
//            links[i].InitBuffer(type_nbytes, count, reduce_buffer_size);
//      }
//      links[i].ResetSize();
//  }
//  // if no childs, no need to reduce
//  if (nlink == static_cast<int>(parent_index != -1)) {
//      size_up_reduce = total_size;
//  }
//  // while we have not passed the messages out
//  while (true) {
//     bool finished = true;
//     for (int i = 0; i < nlink; ++i) {
//          if (i == parent_index) {
//             if (size_down_in != total_size) {
//             finished = false;
//          }
//     } else {
//          // size_write <= size_read
//          if (links[i].size_write != total_size) {
//              // only watch for exception in live channel->
//              finished = false;
//          }
//      }
//    }
//    // finish runing allreduce
//    if (finished) break;
//    // read data from childs
//    for (int i = 0; i < nlink; ++i) {
//      if (i != parent_index) {
//        ReturnType ret = links[i].ReadToRingBuffer(size_up_out, total_size);
//        if (ret != Status::kSuccess) {
//          return ReportError(&links[i], ret);
//        }
//      }
//    }
//    // this node have childs, peform reduce
//    if (nlink > static_cast<int>(parent_index != -1)) {
//      size_t buffer_size = 0;
//      // do upstream reduce
//      size_t max_reduce = total_size;
//      for (int i = 0; i < nlink; ++i) {
//        if (i != parent_index) {
//          max_reduce = std::min(max_reduce, links[i].size_read);
//          utils::CHECK_F(buffer_size == 0 || buffer_size == links[i].buffer_size,
//                        "buffer size inconsistent");
//          buffer_size = links[i].buffer_size;
//        }
//      }
//      utils::CHECK_F(buffer_size != 0, "must assign buffer_size");
//      // round to type_n4bytes
//      max_reduce = (max_reduce / type_nbytes * type_nbytes);
//      // peform reduce, can be at most two rounds
//      while (size_up_reduce < max_reduce) {
//        // start position
//        size_t start = size_up_reduce % buffer_size;
//        // peform read till end of buffer
//        size_t nread = std::min(buffer_size - start,
//                                max_reduce - size_up_reduce);
//        utils::CHECK_F(nread % type_nbytes == 0, "Allreduce: size check");
//        for (int i = 0; i < nlink; ++i) {
//          if (i != parent_index) {
//            reducer(links[i].buffer_head + start,
//                    sendrecvbuf + size_up_reduce,
//                    static_cast<int>(nread / type_nbytes),
//                    MPI::Datatype(type_nbytes));
//          }
//        }
//        size_up_reduce += nread;
//      }
//    }
//    if (parent_index != -1) {
//      // pass message up to parent, can pass data that are already been reduced
//      if (size_up_out < size_up_reduce) {
//        ssize_t len = links[parent_index].sock.
//            Send(sendrecvbuf + size_up_out, size_up_reduce - size_up_out);
//        if (len != -1) {
//          size_up_out += static_cast<size_t>(len);
//        } else {
//          ReturnType ret = Errno2Return();
//          if (ret != Status::kSuccess) {
//            return ReportError(&links[parent_index], ret);
//          }
//        }
//      }
//      // read data from parent
//      if (total_size > size_down_in) {
//        ssize_t len = links[parent_index].sock.
//            Recv(sendrecvbuf + size_down_in, total_size - size_down_in);
//        if (len == 0) {
//          links[parent_index].sock.Close();
//          return ReportError(&links[parent_index], kRecvZeroLen);
//        }
//        if (len != -1) {
//          size_down_in += static_cast<size_t>(len);
//          CHECK_F(size_down_in <= size_up_out,
//                        "Allreduce: boundary error");
//        } else {
//          ReturnType ret = Errno2Return();
//          if (ret != Status::kSuccess) {
//            return ReportError(&links[parent_index], ret);
//          }
//        }
//      }
//    } else {
//      // this is root, can use reduce as most recent point
//      size_down_in = size_up_out = size_up_reduce;
//    }
//    // can pass message down to childs
//    for (int i = 0; i < nlink; ++i) {
//      if (i != parent_index && links[i].size_write < size_down_in) {
//        ReturnType ret = links[i].WriteFromArray(sendrecvbuf, size_down_in);
//        if (ret != Status::kSuccess) {
//          return ReportError(&links[i], ret);
//        }
//      }
//    }
//  }
//  return Status::kSuccess;
}
void AllreduceBase::TryBroadcast(void *sendrecvbuf_, size_t total_size, int root) {
//    auto& links = tree_links;
//    if (links.size() == 0 || total_size == 0) return;
//    CHECK_F(root < world_size,
//                 "Broadcast: root should be smaller than world size");
//    // number of links
//    const int nlink = static_cast<int>(links.size());
//    // size of space already read from data
//    size_t size_in = 0;
//    // input link, -2 means unknown yet, -1 means this is root
//    int in_link = -2;
//
//    // initialize the link statistics
//    for (int i = 0; i < nlink; ++i) {
//        links[i].ResetSize();
//    }
//    // root have all the data
//    if (this->rank == root) {
//        size_in = total_size;
//        in_link = -1;
//    }
//    // while we have not passed the messages out
//    while (true) {
//        bool finished = true;
//        std::vector<bool> read_flags(nlink, false), \
//        write_flags(nlink, false);
//    for (int i = 0; i < nlink; ++i) {
//      if (in_link == -2) {
//        read_flags[i] = true;
//        finished = false;
//      }
//      if (i == in_link && links[i].size_read != total_size) {
//          read_flags[i] = true;
//          finished = false;
//      }
//      if (in_link != -2 && i != in_link && links[i].size_write != total_size) {
//        if (links[i].size_write < size_in) {
//            write_flags[i] = true;
//        }
//        finished = false;
//      }
//    }
//    // finish running
//    if (finished) break;
//    // select
//    // exception handling
//    if (in_link == -2) {
//      // probe in-link
//      for (int i = 0; i < nlink; ++i) {
//        if (read_flags[i] == true) {
//          ReturnType ret = links[i].ReadToArray(sendrecvbuf_, total_size);
//          size_in = links[i].size_read;
//          if (size_in != 0) {
//            in_link = i; break;
//          }
//        }
//      }
//    } else {
//      // read from in link
//      if (in_link >= 0 && read_flags[in_link]) {
//        ReturnType ret = links[in_link].ReadToArray(sendrecvbuf_, total_size);
//        size_in = links[in_link].size_read;
//      }
//    }
//    // send data to all out-link
//    for (int i = 0; i < nlink; ++i) {
//      if (i != in_link && links[i].size_write < size_in) {
//        ReturnType ret = links[i].WriteFromArray(sendrecvbuf_, size_in);
//      }
//    }
//  }
//  return Status::kSuccess;
}
void AllreduceBase::TryAllgatherRing(void** sendrecvbufs_, size_t type_nbytes,
                                     size_t* counts) {
    // read from next link and send to prev one
    auto &prev = *ring_prev, &next = *ring_next;
    // need to reply on special rank structure
    CHECK_F(next.rank == (rank + 1) % world_size &&
           rank == (prev.rank + 1) % world_size,
           "need to assume rank structure");
    const size_t count_bufs = world_size;
    const size_t stop_write_idx = count_bufs + rank - 1;
    const size_t stop_read_idx = count_bufs + rank;
    size_t write_idx = rank;
    size_t read_idx = rank + 1;
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
            auto wc = prev.channel->ISend(*(sendrecvbufs_ + start),
                                          counts[start] * type_nbytes);
            wc.Wait();
            write_idx++;
        }
        if (read_idx != stop_read_idx) {
            size_t start = read_idx % count_bufs;
            auto wc = next.channel->IRecv(*(sendrecvbufs_ + start),
                                          counts[start] * type_nbytes);
            wc.Wait();
            read_idx++;
        }
    }
}
void AllreduceBase::TryReduceScatterRing(void *sendrecvbuf_,
                                    void* reducebuf_,
                                    size_t type_nbytes,
                                    size_t count,
                                    ReduceFunction reducer) {
    // read from next link and send to prev one
    auto& prev = *ring_prev, &next = *ring_next;
    // need to reply on special rank structure
    CHECK_F(next.rank == (rank + 1) % world_size &&
            rank == (prev.rank + 1) % world_size,
            "need to assume rank structure");
    // total size of message
    const size_t total_size = type_nbytes * count;
    size_t n = static_cast<size_t>(world_size);
    const auto& ranges = utils::Split(0, count, n);
    size_t next_rank = static_cast<size_t>(next.rank);
    size_t write_idx = next_rank;
    size_t read_idx = next_rank + 1;
    size_t reduce_idx = read_idx;
    // send recv buffer
    char* sendrecvbuf = reinterpret_cast<char*>(sendrecvbuf_);
    // reduce buffer
    char* reducebuf = reinterpret_cast<char*>(reducebuf_);
    // position to stop reading
    const size_t stop_read_idx = n + next_rank;
    // position to stop writing
    size_t stop_write_idx = n + rank;
    if (stop_write_idx > stop_read_idx) {
        stop_write_idx -= n;
        CHECK_F(write_idx <= stop_write_idx, "write ptr boundary check");
    }
    // use ring buffer in next position
    //next.InitBuffer(type_nbytes, step);
    // set size_read to read pointer for ring buffer to work properly
    //next.size_read = read_ptr;
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
            //size_t size = std::min(reduce_ptr, stop_write) - write_ptr;
            size_t write_size = (ranges[write_pos].second -
                                ranges[write_pos].first) * type_nbytes;
            size_t write_start = write_idx % n * type_nbytes;
            //if (start + size > total_size) {
            //    size = total_size - start;
            //}
            auto wc = prev.channel->ISend(sendrecvbuf + write_start, write_size);
            wc.Wait();
            write_idx ++;
            //int* a = reinterpret_cast<int*>(sendrecvbuf + start);
            //LOG_S(INFO) << "@node:" << rabit::GetRank() <<  " send:" << a[0];
        }
        if (read_idx != stop_read_idx) {
            //LOG_F(INFO,"%d",rabit::GetRank());
            //next.ReadToRingBuffer(reduce_ptr, stop_read);
            // sync the rate
            //read_ptr = next.size_read;
            size_t read_pos = read_idx % n;
            size_t read_start = read_pos * type_nbytes;
            size_t read_size = (ranges[read_pos].second -
                               ranges[read_pos].first) * type_nbytes;
            auto wc = next.channel->IRecv(reducebuf + read_start, read_size);
            wc.Wait();
            read_idx++;
            CHECK_F(read_idx <= stop_read_idx,"[%d] read_ptr boundary check",
                     rank);
            size_t reduce_pos = reduce_idx % n;
            size_t reduce_start = reduce_pos * type_nbytes;
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
void AllreduceBase::TryAllreduceRing(void *sendrecvbuf_,
                                size_t type_nbytes,
                                size_t count,
                                ReduceFunction reducer) {
    void* reducebuf = malloc(count * type_nbytes);
    TryReduceScatterRing(sendrecvbuf_, reducebuf, type_nbytes, 
                         count, reducer);
    free(reducebuf);
    int* a = reinterpret_cast<int*>(sendrecvbuf_);
    LOG_F(INFO, "@node: %d after reduce scatter %d %d %d %d",rank, a[0], a[1], a[2], a[3]);
    size_t n = static_cast<size_t>(world_size);
    size_t step = (count + n - 1) / n;
    size_t begin = std::min(rank * step, count) * type_nbytes;
    size_t end = std::min((rank + 1) * step, count) * type_nbytes;
    // previous rank
    int prank = ring_prev->rank;
    // get rank of previous
    std::vector<void*> sendrecv_bufs(n);
    std::vector<size_t> sizes(n);
    for (int i = 0; i < n; i++) {
        size_t begin = std::min(i * step, n);
        size_t end = std::min((i + 1) * step, n);
        size_t size = end - begin;
        LOG_F(INFO, "%d %d", begin, size);
        sizes[i] = size;
        if (i == 2) sizes[i] = 1;
        sendrecv_bufs[i] = utils::IncrVoidPtr(sendrecvbuf_, begin * type_nbytes);
    }
    return TryAllgatherRing(utils::BeginPtr(sendrecv_bufs), type_nbytes, 
                            utils::BeginPtr(sizes));
}

void AllreduceBase::Send(void* sendbuf_, size_t type_nbytes, int dest) {
//  all_links_by_rank[dest].sock.SendAll(sendbuf_, type_nbytes);
}
void AllreduceBase::Recv(void* recvbuf_, size_t type_nbytes, int src)  {
//  all_links_by_rank[src].sock.RecvAll(recvbuf_, type_nbytes);
}
}  // namespace engine
}  // namespace rabit
