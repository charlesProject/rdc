/*!
 * Copyright by Contributors
 * \file rdc-inl.h
 * \brief implementation of inline template function for rdc interface
 *
 * \author Ankun Zheng
 */
#pragma once
// use comm for implementation
#include <vector>
#include <string>
#include "rdc.h"
#include "utils/utils.h"
#include "core/mpi.h"
#include "io/io.h"

namespace rdc {
// intialize the rdc comm
inline void Init(int argc, char *argv[]) {
    comm::Init(argc, argv);
}
inline void NewCommunicator(const std::string& name) {
    comm::GetCommunicator()->NewCommunicator(name);
}
// finalize the rdc comm
inline void Finalize() {
    comm::Finalize();
}
// get the rank of current process
inline int GetRank(void) {
    return comm::GetCommunicator()->GetRank();
}
// the the size of the world
inline int GetWorldSize(void) {
    return comm::GetCommunicator()->GetWorldSize();
}
// whether rdc is distributed
inline bool IsDistributed(void) {
    return comm::GetCommunicator()->IsDistributed();
}
// get the name of current processor
inline std::string GetProcessorName(void) {
    return comm::GetCommunicator()->GetHost();
}
inline void Send(const Buffer& sendbuf, int dest) {
    comm::GetCommunicator()->Send(sendbuf, dest);
}
inline void Recv(Buffer& recvbuf, int src) {
    comm::GetCommunicator()->Recv(recvbuf, src);
}
inline void Send(void* send_data, uint64_t size, int dest) {
    comm::GetCommunicator()->Send(send_data, size, dest);
}
inline void Recv(void* recv_data, uint64_t size, int src) {
    comm::GetCommunicator()->Recv(recv_data, size, src);
}
inline void Barrier() {
    comm::GetCommunicator()->Barrier();
}
// broadcast data to all other nodes from root
inline void Broadcast(Buffer& sendrecvbuf, int root,
        const std::string& comm_name) {
    comm::GetCommunicator(comm_name)->Broadcast(sendrecvbuf, root);
}
inline void Broadcast(void* sendrecvaddr, uint64_t size, int root,
        const std::string& comm_name) {
    comm::GetCommunicator(comm_name)->Broadcast(sendrecvaddr, size, root);
}
template<typename DType>
inline void Broadcast(std::vector<DType>& sendrecv_data, int root,
        const std::string& comm_name) {
    uint64_t size = sendrecv_data.size();
    Broadcast(&size, sizeof(size), root, comm_name);
    if (sendrecv_data.size() != size) {
        sendrecv_data.resize(size);
    }
    if (size != 0) {
        Broadcast({utils::BeginPtr(sendrecv_data),
            size * sizeof(DType)}, root, comm_name);
    }
}
inline void Broadcast(std::string& sendrecv_data, int root,
        const std::string& comm_name) {
    size_t size = sendrecv_data.length();
    Broadcast(&size, sizeof(size), root);
    if (sendrecv_data.length() != size) {
        sendrecv_data.resize(size);
    }
    if (size != 0) {
        Broadcast(utils::BeginPtr(sendrecv_data),
            size * sizeof(char), root, comm_name);
    }
}
inline void Allgather(std::vector<Buffer>& sendrecvbufs,
        const std::string& comm_name) {
    comm::GetCommunicator(comm_name)->Allgather(sendrecvbufs);
}
template<typename DType>
inline void Allgather(std::vector<std::vector<DType>>& sendrecv_data,
        const std::string& comm_name) {
    std::vector<Buffer> sendrecvbufs(sendrecv_data.size());
    for (auto i = 0U; i < sendrecv_data.size(); ++i) {
        sendrecvbufs[i].set_addr(reinterpret_cast<void*>(
                    utils::BeginPtr(sendrecv_data[i])));
        sendrecvbufs[i].set_size_in_bytes(sendrecv_data[i].size() *
            sizeof(DType));
    }
    Allgather(sendrecvbufs, comm_name);
}

// perform inplace Allreduce
template<typename OP, typename DType>
inline void Allreduce(DType* sendrecvbuf_, uint64_t count,
        const std::string& comm_name) {
    Buffer sendrecvbuf(sendrecvbuf_, count * sizeof(DType));
    sendrecvbuf.set_type_nbytes(sizeof(DType));
    auto reducer = [] (Buffer src, Buffer dst) {
        op::Reducer<OP, DType>(src.addr(), dst.addr(), src.Count());
    };
    comm::Allreduce_(sendrecvbuf, reducer,
            mpi::GetType<DType>(), OP::kType, comm_name);
}

// print message to the tracker
inline void TrackerPrint(const std::string &msg) {
    comm::GetCommunicator()->TrackerPrint(msg);
}
// load latest check point
inline int LoadCheckPoint(Serializable *global_model,
                          Serializable *local_model) {
    return comm::GetCommunicator()->LoadCheckPoint(
            global_model, local_model);
}
// checkpoint the model, meaning we finished a stage of execution
inline void CheckPoint(const Serializable* global_model,
        const Serializable* local_model) {
    comm::GetCommunicator()->CheckPoint(global_model, local_model);
}
// lazy checkpoint the model, only remember the pointer to global_model
inline void LazyCheckPoint(const Serializable *global_model) {
    comm::GetCommunicator()->LazyCheckPoint(global_model);
}
// return the version number of currently stored model
inline int VersionNumber(void) {
    return comm::GetCommunicator()->VersionNumber();
}

inline std::unique_ptr<comm::ICommunicator> CreateGroup(
        const std::vector<int>& ranks,
        const std::string& group_name) {
    return comm::GetCommunicator()->CreateGroup(ranks, group_name);
}

// ---------------------------------
// Code to handle customized Reduce
// ---------------------------------
// function to perform reduction for Reducer
template<typename DType, void (*freduce)(DType &dst, const DType &src)>
inline void ReducerSafe_(const void *src_, void *dst_,
                         int len_, const MPI::Datatype &dtype) {
    const size_t kUnit = sizeof(DType);
    const char *psrc = reinterpret_cast<const char*>(src_);
    char *pdst = reinterpret_cast<char*>(dst_);
    DType tdst, tsrc;
    for (int i = 0; i < len_; ++i) {
        // use memcpy to avoid alignment issue
        std::memcpy(&tdst, pdst + i * kUnit, sizeof(tdst));
        std::memcpy(&tsrc, psrc + i * kUnit, sizeof(tsrc));
        freduce(tdst, tsrc);
        std::memcpy(pdst + i * kUnit, &tdst, sizeof(tdst));
    }
}
// function to perform reduction for Reducer
template<typename DType, void (*freduce)(DType &dst, const DType &src)> // NOLINT(*)
inline void ReducerAlign_(const void *src_, void *dst_,
                          int len_, const MPI::Datatype &dtype) {
    const DType *psrc = reinterpret_cast<const DType*>(src_);
    DType *pdst = reinterpret_cast<DType*>(dst_);
    for (int i = 0; i < len_; ++i) {
        freduce(pdst[i], psrc[i]);
    }
}
// closure to call Allreduce
template<typename DType>
struct SerializeReduceClosure {
    DType *sendrecvobj;
    size_t max_nbyte, count;
    std::string *p_buffer;
    // invoke the closure
    inline void Run(void) {
        for (size_t i = 0; i < count; ++i) {
            MemoryFixSizeBuffer fs(utils::BeginPtr(*p_buffer) + i * max_nbyte, max_nbyte);
            sendrecvobj[i].Save(fs);
        }
    }
    inline static void Invoke(void *c) {
        static_cast<SerializeReduceClosure<DType>*>(c)->Run();
    }
};
template<typename DType, void (*freduce)(DType &dst, const DType &src)>  // NOLINT(*)
inline void Reducer<DType, freduce>::Allreduce(
        DType *sendrecvbuf, size_t count) {
    this->Allreduce(sendrecvbuf, count);
}
template<typename DType>
inline void SerializeReducer<DType>::Allreduce(DType *sendrecvobj,
                                               size_t max_nbytes, 
                                               size_t count) {
    this->Allreduce(sendrecvobj, max_nbytes, count);
}
}  // namespace rdc
