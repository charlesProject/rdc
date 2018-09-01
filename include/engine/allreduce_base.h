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
#include "core/status.h"
#include "core/logging.h"
#include "engine/engine.h"
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
namespace engine {
/*! \brief implementation of basic Allreduce engine */
class Communicator : public IEngine {
public:
    // magic number to verify server
    static const int kMagic = 0xff99;
    // constant one byte out of band message to indicate error happening
    Communicator(void);
    virtual ~Communicator(void) {}
    // initialize the manager
    virtual void Init(int argc, char* argv[]);
    // shutdown the engine
    virtual void Shutdown(void);
    /*!
     * \brief set parameters to the engine
     * \param name parameter name
     * \param val parameter value
     */
    virtual void SetParam(const char *name, const char *val);
    /*!
     * \brief print the msg in the tracker,
     *    this function can be used to communicate the information of the progress to
     *    the user who monitors the tracker
     * \param msg message to be printed in the tracker
     */
    virtual void TrackerPrint(const std::string &msg);
    /*! \brief get rank */
    virtual int GetRank(void) const {
      return rank;
    }
    /*! \brief get rank */
    virtual int GetWorldSize(void) const {
      if (world_size == -1) return 1;
      return world_size;
    }
    /*! \brief whether is distributed or not */
    virtual bool IsDistributed(void) const {
      return tracker_uri != "NULL";
    }
    /*! \brief get rank */
    virtual std::string GetHost(void) const {
      return host_uri;
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
     * \param prepare_func Lazy preprocessing function, lazy prepare_fun(prepare_arg)
     *                     will be called by the function before performing Allreduce, to intialize the data in sendrecvbuf_.
     *                     If the result of Allreduce can be recovered directly, then prepare_func will NOT be called
     * \param prepare_arg argument used to passed into the lazy preprocessing function
     */
    virtual void Allreduce(void *sendrecvbuf_,
                           size_t type_nbytes,
                           size_t count,
                           ReduceFunction reducer,
                           PreprocFunction prepare_fun = nullptr,
                           void *prepare_arg = nullptr) {
        if (prepare_fun != nullptr) prepare_fun(prepare_arg);
        if (world_size == 1 || world_size == -1) return;
        TryAllreduce(sendrecvbuf_, type_nbytes, count, reducer);
    }
    /*!
     * \brief broadcast data from root to all nodes
     * \param sendrecvbuf_ buffer for both sending and recving data
     * \param size the size of the data to be broadcasted
     * \param root the root worker id to broadcast the data
     */
    virtual void Broadcast(void* sendrecvbuf_, size_t total_size, int root) {
        if (world_size == 1 || world_size == -1) return;
        TryBroadcast(sendrecvbuf_, total_size, root);
    }
    virtual void Allgather(void** sendrecvbufs_, size_t type_nbytes,
                           size_t* counts) override {
        if (world_size == 1 || world_size == -1) return;
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
                               Serializable *local_model = NULL) {
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
     *    call this function when IEngine throw an exception out,
     *    this function is only used for test purpose
     */
    virtual void InitAfterException(void) {
        LOG_F(ERROR, "InitAfterException: not implemented");
    }

   protected:
    // link record to a neighbor
    struct LinkRecord {
    public:
        // socket to get data from/to link
        std::unique_ptr<IChannel> channel;
        // rank of the node in this link
        int rank;
        // size of data readed from link
        size_t size_read;
        // size of data sent to the link
        size_t size_write;
        // pointer to buffer head
        char *buffer_head;
        // buffer size, in bytes
        size_t buffer_size;
        // constructor
        LinkRecord()
            : buffer_head(nullptr), buffer_size(0), buffer_(nullptr) {
        }
        LinkRecord(LinkRecord&& other) {
            this->channel = std::move(other.channel);
            this->rank = other.rank;
            this->size_read = other.size_read;
            this->size_write = other.size_write;
            this->buffer_size = other.buffer_size;
            this->buffer_head = other.buffer_head;
            this->buffer_  = other.buffer_;
        }

        LinkRecord& operator=(LinkRecord&& other) {
            this->channel = std::move(other.channel);
            this->rank = other.rank;
            this->size_read = other.size_read;
            this->size_write = other.size_write;
            this->buffer_size = other.buffer_size;
            this->buffer_head = other.buffer_head;
            this->buffer_  = other.buffer_;
            return *this;
        }
        ~LinkRecord() {
            if (buffer_) {
                delete[] buffer_;
            }
        }
        // initialize buffer
        inline void InitBuffer(size_t type_nbytes, size_t count) {
            //size_t n = (type_nbytes * count + 7)/ 8;
            size_t n = type_nbytes * count;
            //buffer_.resize(n);
            buffer_size = n;
            buffer_ = new char[buffer_size];
            // make sure align to type_nbytes
            //buffer_size = buffer_.size() * sizeof(uint64_t) / type_nbytes * type_nbytes;
            CHECK_F(type_nbytes <= buffer_size,
                          "too large type_nbytes=%lu, buffer_size=%lu",
                          type_nbytes, buffer_size);
            // set buffer head
            buffer_head = buffer_;
        }
        // reset the recv and sent size
        inline void ResetSize(void) {
            size_write = size_read = 0;
        }
        /*!
         * \brief read data into ring-buffer, with care not to existing useful override data
         *  position after protect_start
         * \param protect_start all data start from protect_start is still needed in buffer
         *                      read shall not override this
         * \param max_size_read maximum logical amount we can read, size_read cannot exceed this value
         */
        inline void ReadToRingBuffer(size_t protect_start, size_t max_size_read) {
            CHECK_F(buffer_head != nullptr, "ReadToRingBuffer: buffer not allocated");
            CHECK_F(size_read <= max_size_read, "ReadToRingBuffer: max_size_read check");
            size_t ngap = size_read - protect_start;
            CHECK_F(ngap <= buffer_size, "Allreduce: boundary check");
            size_t offset = size_read % buffer_size;
            size_t nmax = max_size_read - size_read;
            nmax = std::min(nmax, buffer_size - ngap);
            nmax = std::min(nmax, buffer_size - offset);
            LOG_F(INFO, "%d %d", rdc::GetRank(), nmax);
            if (nmax == 0) {
                return;
            //    LOG_F(INFO, "no space for data receiving");
            }
            auto wc = channel->IRecv(buffer_head + offset, nmax);
            wc.Wait();
            //int* a = reinterpret_cast<int*>(buffer_head + offset);
            const size_t len = wc.completed_bytes();
            //LOG_S(INFO) << "@node:" << rdc::GetRank() <<  " recv:" << 
            //    a[0] << " size:" << nmax;
            size_read += static_cast<size_t>(len);
            return;
        }
        /*!
         * \brief read data into array,
         * this function can not be used together with ReadToRingBuffer
         * a link can either read into the ring buffer, or existing array
         * \param max_size maximum size of array
         * \return true if it is an successful read, false if there is some error happens, check errno
         */
        inline void ReadToArray(void *recvbuf_, size_t max_size) {
            if (max_size == size_read) return;
            char *p = static_cast<char*>(recvbuf_);
            auto wc = channel->IRecv(p + size_read, max_size - size_read);
            wc.Wait();
            const size_t len = wc.completed_bytes();
            size_read += static_cast<size_t>(len);
            return;
        }
        /*!
         * \brief write data in array to channel
         * \param sendbuf_ head of array
         * \param max_size maximum size of array
         * \return true if it is an successful write, false if there is some error happens, check errno
         */
        inline void WriteFromArray(const void *sendbuf_, size_t max_size) {
            const char *p = static_cast<const char*>(sendbuf_);
            auto wc = channel->ISend(p + size_write, max_size - size_write);
            wc.Wait();
            const size_t& len = wc.completed_bytes();
            size_write += static_cast<size_t>(len);
            return;
        }

       private:
          // recv buffer to get data from child
          // aligned with 64 bits, will be able to perform 64 bits operations freely
          char* buffer_;
    };
    /*!
     * \brief initialize connection to the tracker
     * \return a channel that initializes the connection
     */
    void ConnectTracker();
    /*!
     * \brief connect to the tracker to fix the the missing links
     *   this function is also used when the engine start up
     * \param cmd possible command to sent to tracker
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
     *  where step = ceil(count / world_size)
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
    // channel for communication with tracker
    std::unique_ptr<TcpChannel> tracker;
    // call sequence counter, records how many calls we made so far
    // from last call to CheckPoint, LoadCheckPoint
    int seq_counter;
    // version number of model
    int version_number;
    //---- local data related to link ----
    // index of parent link, can be -1, meaning this is root of the tree
    int parent_index;
    // rank of parent node, can be -1
    int parent_rank;
    // channels of all links referenced by rank
    std::unordered_map<int, LinkRecord> all_links;
    // used to record the link where things goes wrong
    LinkRecord *err_link;
    // all the links in the reduction tree connection
    std::vector<LinkRecord*> tree_links;
    // pointer to links in the ring
    LinkRecord *ring_prev, *ring_next;
    //----- meta information-----
    // list of enviroment variables that are of possible interest
    std::vector<std::string> env_vars;
    // unique identifier of the possible job this process is doing
    // used to assign ranks, optional, default to NULL
    std::string task_id;
    // uri of current host, to be set by Init
    std::string host_uri;
    // uri of tracker
    std::string tracker_uri;
    // port of tracker address
    int tracker_port;
    // port of slave process
    int slave_port, nport_trial;
    // reduce buffer size
    size_t reduce_buffer_size;
    // reduction method
    int reduce_method;
    // mininum count of cells to use ring based method
    size_t reduce_ring_mincount;
    // current rank
    int rank;
    // world size
    int world_size;
    // connect retry time
    int connect_retry;
};
}  // namespace engine
}  // namespace rdc
