/*!
 *  Copyright (c) 2018 by Contributors
 * \file comm.cc
 * \brief this file governs which implementation of comm we are actually using
 *  provides an singleton of comm interface
 *
 * \author Ankun Zheng
 */
#include <memory>
#include "comm/communicator.h"
#include "comm/communicator_base.h"
#include "core/thread_local.h"
namespace rdc {
namespace comm {
// singleton sync manager
//#ifndef RDC_USE_BASE
//typedef CommunicatorRobust Comm;
//#else
typedef Communicator Comm;
//#endif

/*! \brief entry to to easily hold returning information */
struct ThreadLocalEntry {
    /*! \brief stores the current comm */
    std::unordered_map<std::string, std::unique_ptr<Comm>> comms;
    /*! \brief constructor */
    ThreadLocalEntry() {
        lock = utils::make_unique<std::mutex>();
    }
    void AddComm(const std::string& name) {
        lock->lock();
        CHECK_F(!comms.count(name), "Init is already called in this thread");
        comms[name] = utils::make_unique<Comm>(name);
        lock->unlock();
    }
    Comm* GetComm(const std::string& name) {
        std::lock_guard<std::mutex> lg(*lock);
        return comms[name].get();
    }
    std::unique_ptr<std::mutex> lock;
};

// define the threadlocal store.
typedef ThreadLocalStore<ThreadLocalEntry> ThreadLocalCommunicator;

/*! \brief intiialize the synchronization module */
void Init(int argc, char *argv[], const std::string& name) {
    ThreadLocalEntry* e = ThreadLocalCommunicator::Get();
    e->AddComm(name);
    e->GetComm(name)->Init(argc, argv);
}

/*! \brief finalize syncrhonization module */
void Finalize(const std::string& name) {
    ThreadLocalEntry* e = ThreadLocalCommunicator::Get();
    CHECK_F(e->GetComm(name) != nullptr,
                 "rdc::Finalize comm is not initialized \
                 or already been finalized.");
    e->GetComm(name)->Shutdown();
}

/*! \brief singleton method to get comm */
ICommunicator *GetCommunicator(const std::string& name) {
    // un-initialized default manager.
    static Communicator default_manager;
    ThreadLocalEntry* e = ThreadLocalCommunicator::Get();
    ICommunicator* ptr = e->GetComm(name);
    if (ptr == nullptr) {
        return &default_manager;
    } else {
        return ptr;
    }
}
// perform in-place allreduce, on sendrecvbuf
void Allreduce_(void *sendrecvbuf,
                size_t type_nbytes,
                size_t count,
                ICommunicator::ReduceFunction red,
                mpi::DataType dtype,
                mpi::OpType op) {
    GetCommunicator()->Allreduce(sendrecvbuf, type_nbytes, count,
                           red);
}

}  // namespace comm
}  // namespace rdc
