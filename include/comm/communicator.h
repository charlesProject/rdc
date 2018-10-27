/*!
 *  Copyright (c) 2018 by Contributors
 * \file comm.h
 * \brief This file defines the core interface of rdc library
 * \author Ankun Zheng
 */
#pragma once
#include <string>
#include <memory>
#include "io/io.h"
#include "core/mpi.h"
#include "core/work_request.h"
#include "transport/buffer.h"
namespace MPI {
/*! \brief MPI data type just to be compatible with MPI reduce function*/
class Datatype;
}

/*! \brief namespace of rdc */
namespace rdc {
const std::string kWorldCommName = "main";
/*! \brief core interface of the comm */
namespace comm {
/*!
 * \brief reduce function, the same form of MPI reduce function is used,
 *        to be compatible with MPI interface
 *        In all the functions, the memory is ensured to aligned to 64-bit
 *        which means it is OK to cast src,dst to double* int* etc
 * \param src pointer to source space
 * \param dst pointer to destination reduction
 * \param count total number of elements to be reduced (note this is total number of elements instead of bytes)
 *              the definition of the reduce function should be type aware
 * \param dtype the data type object, to be compatible with MPI reduce
 */
using ReduceFunction = std::function<void(Buffer src, Buffer dst)>;
using RawReduceFunction = std::function<void(const void* src, void* dst, uint64_t len)>;


/*! \brief interface of core Allreduce comm */
class ICommunicator {
public:
    /*! \brief virtual destructor */
    virtual ~ICommunicator() {}
    virtual ICommunicator* NewCommunicator(const std::string& name) = 0;
    virtual ICommunicator* GetCommunicator(const std::string& name) = 0;
    virtual void Send(Buffer sendbuf, int dest) = 0;
    virtual void Recv(Buffer recvbuf, int src) = 0;
    void Send(void* sendaddr, uint64_t size_in_bytes, int dest) {
        Buffer sendbuf(sendaddr, size_in_bytes);
        return this->Send(sendbuf, dest);
    }
    void Recv(void* recvaddr, uint64_t size_in_bytes, int src) {
        Buffer recvbuf(recvaddr, size_in_bytes);
        return this->Recv(recvbuf, src);
    }
    virtual WorkCompletion* ISend(Buffer sendbuf, int dest) = 0;
    virtual WorkCompletion* IRecv(Buffer recvbuf, int src) = 0;
    WorkCompletion* ISend(void *sendaddr, uint64_t size_in_bytes, int dest) {
        Buffer sendbuf(sendaddr, size_in_bytes);
        return this->ISend(sendbuf, dest);
    }
    WorkCompletion* IRecv(void* recvaddr, uint64_t size_in_bytes, int src) {
        Buffer recvbuf(recvaddr, size_in_bytes);
        return this->IRecv(recvbuf, src);
    }
    virtual void Barrier() = 0;
    /*!
     * \brief performs in-place Allreduce, on sendrecvbuf
     *        this function is NOT thread-safe
     * \param sendrecvbuf_ buffer for both sending and receiving data
     * \param type_nbytes the number of bytes the type has
     * \param count number of elements to be reduced
     * \param reducer reduce function
     */
    virtual void Allreduce(Buffer sendrecvbuf, ReduceFunction reducer) = 0;
    /*!
     * \brief broadcasts data from root to every other node
     * \param sendrecvbuf_ buffer for both sending and receiving data
     * \param size the size of the data to be broadcasted
     * \param root the root worker id to broadcast the data
     */
    virtual void Broadcast(Buffer sendrecvbuf, int root) = 0;
    void Broadcast(void* sendrecvaddr, uint64_t size, int root) {
        Buffer sendrecvbuf(sendrecvaddr, size);
        Broadcast(sendrecvbuf, root);
    }
    virtual void Allgather(std::vector<Buffer> sendrecvbufs) = 0;
    void Allgather(std::vector<void*> sendrecvbufs_, std::vector<uint64_t> sizes) {
        auto num_bufs = sendrecvbufs_.size();
        std::vector<Buffer> sendrecvbufs(num_bufs);
        for (auto i = 0U; i < num_bufs; i++) {
            sendrecvbufs[i].set_addr(sendrecvbufs_[i]);
            sendrecvbufs[i].set_size_in_bytes(sizes[i]);
        }
        Allgather(sendrecvbufs);
    }
    /*!
     * \brief explicitly re-initialize everything before calling LoadCheckPoint
     *    call this function when ICommunicator throws an exception,
     *    this function should only be used for test purposes
     */
    virtual void InitAfterException() = 0;
    /*!
     * \brief loads the latest check point
     * \param global_model pointer to the globally shared model/state
     *   when calling this function, the caller needs to guarantee that the global_model
     *   is the same in all nodes
     * \param local_model pointer to the local model that is specific to current node/rank
     *   this can be nullptr when no local model is needed
     *
     * \return the version number of the model loaded
     *     if returned version == 0, this means no model has been CheckPointed
     *     the p_model is not touched, users should do necessary initialization by themselves
     */
    virtual int LoadCheckPoint(Serializable* global_model,
            Serializable* local_model = nullptr) = 0;
    /*!
     * \brief checkpoints the model, meaning a stage of execution was finished
     *  every time we call check point, a version number increases by ones
     *
     * \param global_model pointer to the globally shared model/state
     *   when calling this function, the caller needs to guarantee that the global_model
     *   is the same in every node
     * \param local_model pointer to the local model that is specific to current node/rank
     *   this can be nullptr when no local state is needed
     *
     * NOTE: local_model requires explicit replication of the model for fault-tolerance, which will
     *       bring replication cost in CheckPoint function. global_model does not need explicit replication.
     *       So, only CheckPoint with global_model if possible
     *
     * \sa LoadCheckPoint, VersionNumber
     */
    virtual void CheckPoint(const Serializable* global_model,
            const Serializable* local_model = nullptr) = 0;
    /*!
     * \brief This function can be used to replace CheckPoint for global_model only,
     *   when certain condition is met (see detailed explanation).
     *
     *   This is a "lazy" checkpoint such that only the pointer to global_model is
     *   remembered and no memory copy is taken. To use this function, the user MUST ensure that:
     *   The global_model must remain unchanged until the last call of Allreduce/Broadcast in the current version finishes.
     *   In other words, global_model can be changed only between the last call of
     *   Allreduce/Broadcast and LazyCheckPoint in the current version
     *
     *   For example, suppose the calling sequence is:
     *   LazyCheckPoint, code1, Allreduce, code2, Broadcast, code3, LazyCheckPoint
     *
     *   If the user can only change global_model in code3, then LazyCheckPoint can be used to
     *   improve the efficiency of the program.
     * \param global_model pointer to the globally shared model/state
     *   when calling this function, the caller needs to guarantee that global_model
     *   is the same in every node
     * \sa LoadCheckPoint, CheckPoint, VersionNumber
     */
    virtual void LazyCheckPoint(const Serializable* global_model) = 0;
    /*!
     * \return version number of the current stored model,
     *         which means how many calls to CheckPoint we made so far
     * \sa LoadCheckPoint, CheckPoint
     */
    virtual int VersionNumber() const = 0;
    /*! \brief gets rank of current node */
    virtual int GetRank() const = 0;
    /*! \brief gets total number of nodes */
    virtual int GetWorldSize() const = 0;
    /*! \brief whether we run in distribted mode */
    virtual bool IsDistributed() const = 0;
    /*! \brief gets the host name of the current node */
    virtual std::string GetHost() const = 0;
    /*!
     * \brief prints the msg in the tracker,
     *    this function can be used to communicate progress information to
     *    the tracker
     * \param msg message to be printed in the tracker
     */
    virtual void TrackerPrint(const std::string &msg) = 0;

    /*!
     * \brief create a group communicator under this communicator
     * \param ranks ranks of node in this group
     * \param a unique name for this group
     */
    virtual std::unique_ptr<ICommunicator> CreateGroup(
        const std::vector<int>& ranks,
        const std::string& group_name) = 0;
};

/*! \brief initializes the comm module */
void Init(int argc, char *argv[]);
/*! \brief finalizes the comm module */
void Finalize();
/*! \brief singleton method to get comm */
ICommunicator *GetCommunicator(const std::string& name = kWorldCommName);

/*!
 * \brief perform in-place Allreduce, on sendrecvbuf
 *   this is an internal function used by rdc to be able to compile with MPI
 *   do not use this function directly
 * \param sendrecvbuf buffer for both sending and receiving data
 * \param reducer reduce function
 * \param dtype the data type
 * \param op the reduce operator type
 */
void Allreduce_(Buffer sendrecvbuf, ReduceFunction red, mpi::DataType dtype,
        mpi::OpType op, const std::string& comm_name);
}  // namespace comm
}  // namespace rdc
