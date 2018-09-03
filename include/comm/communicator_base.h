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
#include "comm/communicator.h"
#include "transport/channel.h"
#include "transport/tcp/tcpchannel.h"
#include "transport/tcp/tcppoller.h"
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
    // constant one byte out of band message to indicate error happening
    Communicator();
    virtual ~Communicator() {}
    // initialize the manager
    virtual void Init(int argc, char* argv[]);
    // shutdown the comm
    virtual void Shutdown(void);
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
    virtual void TrackerPrint(const std::string &msg);
    /*! \brief get rank */
    virtual int GetRank(void) const {
        return rank_;
    }
    /*! \brief get rank */
    virtual int GetWorldSize(void) const {
        if (world_size_ == -1) return 1;
        return world_size_;
    }
    /*! \brief whether is distributed or not */
    virtual bool IsDistributed(void) const {
        return tracker_uri_ != "NULL";
    }
    /*! \brief get rank */
    virtual std::string GetHost(void) const {
        return host_uri_;
    }
    void Send(void* sendbuf_, size_t type_nbytes, int dest) override;
    void Recv(void* recvbuf_, size_t type_nbytes, int src) override;
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
                               Serializable *local_model = nullptr) {
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
     *
     * NOTE: local_model requires explicit replication of the model for fault-tolerance, which will
     *       bring replication cost in CheckPoint function. global_model do not need explicit replication.
     *       So only CheckPoint with global_model if possible
     *
     * \sa LoadCheckPoint, VersionNumber
     */
    virtual void CheckPoint(const Serializable *global_model,
                            const Serializable *local_model = NULL) {
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
    virtual void LazyCheckPoint(const Serializable *global_model) {
        version_number += 1;
    }
    /*!
     * \return version number of current stored model,
     *         which means how many calls to CheckPoint we made so far
     * \sa LoadCheckPoint, CheckPoint
     */
    virtual int VersionNumber(void) const {
        return version_number;
    }
    /*!
     * \brief explicitly re-init everything before calling LoadCheckPoint
     *    call this function when ICommunicator throw an exception out,
     *    this function is only used for test purpose
     */
    virtual void InitAfterException() {
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
    void ConnectTracker();
    /*!
     * \brief connect to the tracker_ to fix the the missing links
     *   this function is also used when the comm start up
     * \param cmd possible command to sent to tracker_
     */
    void ReConnectLinks(const char *cmd = "start");
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
     * \sa void
     */
    void TryAllreduce(void *sendrecvbuf_,
                            size_t type_nbytes,
                            size_t count,
                            ReduceFunction reducer);
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
    //---- data structure related to model ----
    // channel for communication with tracker_
    std::unique_ptr<TcpChannel> tracker_;
    // call sequence counter, records how many calls we made so far
    // from last call to CheckPoint, LoadCheckPoint
    int seq_counter;
    // version number of model
    int version_number;
    //---- local data related to link ----
    // rank of parent node, can be -1
    int parent_rank_;
    // channels of all links referenced by rank
    std::unordered_map<int, std::shared_ptr<IChannel>> all_links;
    // used to record the link where things goes wrong
    IChannel* err_link;
    graph::UndirectedGraph<int> tree;
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
    // port of slave process
    int slave_port_, nport_trial_;
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
    std::unordered_map<uint32_t, Communicator*> children_;
    // children counter
    uint32_t child_counter_;
};
}  // namespace comm
}  // namespace rdc
