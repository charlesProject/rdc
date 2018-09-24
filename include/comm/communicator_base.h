/*!
 *  Copyright (c) 2018 by Contributors
 * \file allreduce_base.h
 * \brief Basic implementation of AllReduce
 *   using TCP non-block socket or RDMA for communication.
 *
 *   This implementation provides basic utility of Communication Primitives
 *   without considering node failure
 *
 * \author Ankun Zheng
 */
#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include "utils/utils.h"
#include "utils/graph.h"
#include "core/status.h"
#include "core/logging.h"
#include "core/socket.h"
#include "core/work_request.h"
#include "comm/communicator.h"
#include "transport/channel.h"
#include "transport/adapter.h"
#include "transport/tcp/tcp_channel.h"
#include "transport/tcp/tcp_adapter.h"
#ifdef RDC_USE_RDMA
#include "transport/rdma/rdma_channel.h"
#include "transport/rdma/rdma_adapter.h"
#endif
namespace MPI {
// MPI data type to be compatible with existing MPI interface
class Datatype {
public:
    size_t type_size;
    explicit Datatype(size_t type_size) : type_size(type_size) {}
};
}
namespace rdc {
namespace comm {
/*! \brief implementation of basic Allreduce comm */
class Communicator : public ICommunicator {
public:
    Communicator();
    Communicator(const std::string& name);
    Communicator(const Communicator& other);
    virtual ~Communicator() {}
    // initialize the manager
    virtual void Init(int argc, char* argv[]);
    /*!
     * \brief Create a new communicator which takes its own channels
     * \param name communicator name
    */
    virtual void NewCommunicator(const std::string& name) override;
    // shutdown the comm
    virtual void Shutdown();
    /*!
     * \brief set parameters to the comm
     * \param name parameter name
     * \param val parameter value
     */
    virtual void SetParam(const char *name, const char *val);
    /*!
     * \brief print the msg in the tracker_,
     *    this function can be used to communicate the information of the progress to
     *    the user who monitors the tracker_
     * \param msg message to be printed in the tracker_
     */
    virtual void TrackerPrint(const std::string &msg) override;
    virtual ICommunicator* GetCommunicator(const std::string& name) override {
        if (name == kWorldCommName) {
            return this;
        } else {
            std::lock_guard<std::mutex> lg(comm_lock_);
            return this->sub_comms_[name].get();
        }
    }

    /*! \brief get rank */
    virtual int GetRank() const override {
        return rank_;
    }
    /*! \brief get rank */
    virtual int GetWorldSize() const override {
        if (world_size_ == -1) return 1;
        return world_size_;
    }
    /*! \brief whether is distributed or not */
    virtual bool IsDistributed() const override {
        return tracker_uri_ != "NULL";
    }
    /*! \brief get rank */
    virtual std::string GetHost(void) const override {
        return host_uri_;
    }
    /*!
     *   \brief blocking send
     *  \param sendbuf_ buffer need to  send
     *  \param nbytes buffer size in bytes
     *  \param dest destination rank
    */
    void Send(const Buffer& sendbuf_, int dest) override;
    /*!
     *   \brief blocking send
     *  \param sendbuf_ buffer need to  send
     *  \param nbytes buffer size in bytes
     *  \param dest destination rank
    */
    void Recv(Buffer& recvbuf_, int src) override;
    WorkCompletion ISend(const Buffer& sendbuf_, int dest) override;
    WorkCompletion IRecv(Buffer& recvbuf_, int src) override;
    /*! \brief barrier all nodes*/
    void Barrier() override;
    /*! \brief register this communicator to tracker */
    void Register();
    /*!
     * \brief perform in-place allreduce, on sendrecvbuf
     *        this function is NOT thread-safe
     * \param sendrecvbuf_ buffer for both sending and recving data
     * \param type_nbytes the unit number of bytes the type have
     * \param count number of elements to be reduced
     * \param reducer reduce function
     */
    virtual void Allreduce(void *sendrecvbuf_,
                           size_t type_nbytes,
                           size_t count,
                           ReduceFunction reducer) override {
        if (world_size_ == 1 || world_size_ == -1) return;
        TryAllreduce(sendrecvbuf_, type_nbytes, count, reducer);
    }
    /*!
     * \brief broadcast data from root to all nodes
     * \param sendrecvbuf_ buffer for both sending and recving data
     * \param size the size of the data to be broadcasted
     * \param root the root worker id to broadcast the data
     */
    virtual void Broadcast(void* sendrecvbuf_, size_t total_size, 
            int root) override {
        if (world_size_ == 1 || world_size_ == -1) return;
        TryBroadcast(sendrecvbuf_, total_size, root);
    }

    virtual void Allgather(void** sendrecvbufs_, size_t type_nbytes,
                           size_t* counts) override {
        if (world_size_ == 1 || world_size_ == -1) return;
        TryAllgatherRing(sendrecvbufs_, type_nbytes, counts);
    }
    /*!
     * \brief load latest check point
     * \param global_model pointer to the globally shared model/state
     *   when calling this function, the caller need to gauranttees that global_model
     *   is the same in all nodes
     * \param local_model pointer to local model, that is specific to current node/rank
     *   this can be NULL when no local model is needed
     *
     * \return the version number of check point loaded
     *     if returned version == 0, this means no model has been CheckPointed
     *     the p_model is not touched, user should do necessary initialization by themselves
     *
     *   Common usage example:
     *      int iter = rdc::LoadCheckPoint(&model);
     *      if (iter == 0) model.InitParameters();
     *      for (i = iter; i < max_iter; ++i) {
     *        do many things, include allreduce
     *        rdc::CheckPoint(model);
     *      }
     *
     * \sa CheckPoint, VersionNumber
     */
    virtual int LoadCheckPoint(Serializable *global_model,
                               Serializable *local_model = nullptr) override {
        return 0;
    }
    /*!
     * \brief checkpoint the model, meaning we finished a stage of execution
     *  every time we call check point, there is a version number which will increase by one
     *
     * \param global_model pointer to the globally shared model/state
     *   when calling this function, the caller need to gauranttees that global_model
     *   is the same in all nodes
     * \param local_model pointer to local model, that is specific to current node/rank
     *   this can be NULL when no local state is needed
     */
    virtual void CheckPoint(const Serializable *global_model,
                            const Serializable *local_model = NULL) override {
        version_number += 1;
    }
    /*!
     * \brief This function can be used to replace CheckPoint for global_model only,
     *   when certain condition is met(see detailed expplaination).
     *
     *   This is a "lazy" checkpoint such that only the pointer to global_model is
     *   remembered and no memory copy is taken. To use this function, the user MUST ensure that:
     *   The global_model must remain unchanged util last call of Allreduce/Broadcast in current version finishs.
     *   In another words, global_model model can be changed only between last call of
     *   Allreduce/Broadcast and LazyCheckPoint in current version
     *
     *   For example, suppose the calling sequence is:
     *   LazyCheckPoint, code1, Allreduce, code2, Broadcast, code3, LazyCheckPoint
     *
     *   If user can only changes global_model in code3, then LazyCheckPoint can be used to
     *   improve efficiency of the program.
     * \param global_model pointer to the globally shared model/state
     *   when calling this function, the caller need to gauranttees that global_model
     *   is the same in all nodes
     * \sa LoadCheckPoint, CheckPoint, VersionNumber
     */
    virtual void LazyCheckPoint(const Serializable *global_model) override {
        version_number += 1;
    }
    /*!
     * \return version number of current stored model,
     *         which means how many calls to CheckPoint we made so far
     * \sa LoadCheckPoint, CheckPoint
     */
    virtual int VersionNumber(void) const override{
        return version_number;
    }
    /*!
     * \brief explicitly re-init everything before calling LoadCheckPoint
     *    call this function when ICommunicator throw an exception out,
     *    this function is only used for test purpose
     */
    virtual void InitAfterException() override {
        LOG_F(ERROR, "InitAfterException: not implemented");
    }
    std::unique_ptr<ICommunicator> CreateGroup(
            const std::vector<int>& ranks,
            const std::string& group_name) override;
protected:
    /*!
     * \brief initialize connection to the tracker_
     * \return a channel that initializes the connection
     */
    std::tuple<int, int> ConnectTracker(const char* cmd = "start");
    /*!
     * \brief connect to the tracker_ to fix the the missing links
     *   this function is also used when the comm start up
     * \param cmd possible command to sent to tracker_
     */
    void ReConnectLinks(const std::tuple<int, int>& num_conn_accept);
    /*!
     * \brief perform in-place allreduce, on sendrecvbuf, this function can fail, and will return the cause of failure
     *
     * NOTE on Allreduce:
     *    The kSuccess TryAllreduce does NOT mean every node have successfully finishes TryAllreduce.
     *    It only means the current node get the correct result of Allreduce.
     *    However, it means every node finishes LAST call(instead of this one) of Allreduce/Bcast
     *
     * \param sendrecvbuf_ buffer for both sending and recving data
     * \param type_nbytes the unit number of bytes the type have
     * \param count number of elements to be reduced
     * \param reducer reduce function
     * \return this function can return Status::kSuccess, kSockError, kGetExcept, see void for details
     */
    void TryAllreduce(void* sendrecvbuf_,
                            size_t type_nbytes,
                            size_t count,
                            ReduceFunction reducer);


    void TryReduceTree(void* sendrecvbuf_,
                            void* reducebuf_,
                            size_t type_nbytes,
                            size_t count,
                            ReduceFunction reducer,
                            int root);

    /*!
     * \brief broadcast data from root to all nodes, this function can fail,and will return the cause of failure
     * \param sendrecvbuf_ buffer for both sending and receiving data
     * \param size the size of the data to be broadcasted
     * \param root the root worker id to broadcast the data
     * \return this function can return Status::kSuccess, kSockError, kGetExcept, see void for details
     * \sa void
     */
    void TryBroadcast(void* sendrecvbuf_, size_t size, int root);

    /*!
     * \brief perform in-place allreduce, on sendrecvbuf,
     * this function implements tree-shape reduction
     *
     * \param sendrecvbuf_ buffer for both sending and recving data
     * \param type_nbytes the unit number of bytes the type have
     * \param count number of elements to be reduced
     * \param reducer reduce function
     * \return this function can return Status::kSuccess, kSockError, kGetExcept, see void for details
     * \sa void
     */
    void TryAllreduceTree(void *sendrecvbuf_,
                                size_t type_nbytes,
                                size_t count,
                                ReduceFunction reducer);
    /*!
     * \brief internal Allgather function, each node have a segment of data in the ring of sendrecvbuf,
     *  the data provided by current node k is [slice_begin, slice_end),
     *  the next node's segment must start with slice_end
     *  after the call of Allgather, sendrecvbuf_ contains all the contents including all segments
     *  use a ring based algorithm
     *
     * \param sendrecvbufs_ buffers for both sending and receiving data, each node holds one chunk of
     * \buffer at begin, buffers will be passed in the ring
     * \param type_nbytes the unit number of bytes the type have
     * \param count counts of type hold in buffers
     * \return this function can return Status::kSuccess, kSockError, kGetExcept, see void for details
     * \sa void
     */
    void TryAllgatherRing(void** sendrecvbufs_, size_t type_nbytes, 
                          size_t* counts);
    /*!
     * \brief perform in-place allreduce, reduce on the sendrecvbuf,
     *
     *  after the function, node k get k-th segment of the reduction result
     *  the k-th segment is defined by [k * step, min((k + 1) * step,count) )
     *  where step = ceil(count / world_size_)
     *
     * \param sendrecvbuf_ buffer for both sending and recving data
     * \param reducebuf_ buffer for reducing data
     * \param type_nbytes the unit number of bytes the type have
     * \param count number of elements to be reduced
     * \param reducer reduce function
     * \return this function can return Status, see void for details
     * \sa void, TryAllreduce
     */
    void TryReduceScatterRing(void* sendrecvbuf_,
                              void* reducebuf_,
                              size_t type_nbytes,
                              size_t count,
                              ReduceFunction reducer);
    /*!
     * \brief perform in-place allreduce, on sendrecvbuf
     *  use a ring based algorithm, reduce-scatter + allgather
     *
     * \param sendrecvbuf_ buffer for both sending and recving data
     * \param type_nbytes the unit number of bytes the type have
     * \param count number of elements to be reduced
     * \param reducer reduce function
     * \return this function can return Status see void for details
     * \sa void
     */
    void TryAllreduceRing(void *sendrecvbuf_,
                                size_t type_nbytes,
                                size_t count,
                                ReduceFunction reducer);
    std::shared_ptr<TcpSocket> get_trakcer() const {
        return this->tracker_;
    }
    void set_tracker(const std::shared_ptr<TcpSocket>& tracker) {
        this->tracker_ = tracker;
    }
    void set_worker_port(const int& worker_port) {
        this->worker_port_ = worker_port;
    }
    void set_name(const std::string& name) {
        this->name_ = name;
    }
    //---- data structure related to model ----
    // my name
    std::string name_;
    // channel for communication with tracker_
    std::shared_ptr<TcpSocket> tracker_;
    bool tracker_connected_;
    bool tracker_closed_;
    std::shared_ptr<std::mutex> tracker_lock_;
    std::mutex conn_lock_;
    std::condition_variable tracker_cond_;
    // addr of all peers
    std::unordered_map<int, std::string> peer_addrs_;
    // call sequence counter, records how many calls we made so far
    // from last call to CheckPoint, LoadCheckPoint
    int seq_counter;
    // version number of model
    int version_number;
    //---- local data related to link ----
    // rank of parent node, can be -1
    int parent_rank_;
    // channels of all links referenced by rank
    std::unordered_map<int, std::shared_ptr<IChannel>> all_links_;
    // used to record the link where things goes wrong
    IChannel* err_link;
    graph::UndirectedGraph<int> tree_map_;
    // all the links in the reduction tree connection
    std::vector<IChannel*> tree_links;
    // the rank of neighbors 
    std::map<int, int> tree_neighbors_;
    int num_neighbors_;
    // pointer to links in the ring
    IChannel *ring_prev_, *ring_next_;
    int prev_rank_, next_rank_;
    //----- meta information-----
    // list of enviroment variables that are of possible interest
    std::vector<std::string> env_vars_;
    // unique identifier of the possible job this process is doing
    // uri of current host, to be set by Init
    std::string host_uri_;
    // uri of tracker
    std::string tracker_uri_;
    // port of tracker address
    int tracker_port_;
    // port of worker process
    int worker_port_, nport_trial_;
    // reduction method
    int reduce_method;
    // mininum count of cells to use ring based method
    size_t reduce_ring_mincount_;
    // current rank
    int rank_;
    // world size
    int world_size_;
    // connect retry time
    int connect_retry_;
    // children communicators
    std::unordered_map<uint32_t, Communicator*> groups_;
    // children counter
    uint32_t child_counter_;
    int num_conn_, num_accept_;
    std::mutex comm_lock_;
    std::unordered_map<std::string, std::unique_ptr<Communicator>> sub_comms_;
};
}  // namespace comm
}  // namespace rdc
