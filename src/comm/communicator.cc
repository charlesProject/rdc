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
#include "common/thread_local.h"
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
    std::unique_ptr<Comm> comm;
    /*! \brief constructor */
    ThreadLocalEntry() : initialized(false) {}
    /*! \brief whether init has been called */
    bool initialized;
};

// define the threadlocal store.
typedef ThreadLocalStore<ThreadLocalEntry> ThreadLocalCommunicator;

/*! \brief intiialize the synchronization module */
void Init(int argc, char *argv[]) {
    ThreadLocalEntry* e = ThreadLocalCommunicator::Get();
    CHECK_F(e->comm.get() == nullptr,
            "rdc::Init is already called in this thread");
    e->initialized = true;
    e->comm.reset(new Comm(kWorldCommName));
    e->comm->Init(argc, argv);
}

/*! \brief finalize syncrhonization module */
void Finalize() {
    ThreadLocalEntry* e = ThreadLocalCommunicator::Get();
    CHECK_F(e->comm.get() != nullptr,
                 "rdc::Finalize comm is not initialized \
                 or already been finalized.");
    e->comm->Shutdown();
}

/*! \brief singleton method to get comm */
ICommunicator* GetCommunicator(const std::string& name) {
    // un-initialized default communicator.
    static Communicator default_comm;
    ThreadLocalEntry* e = ThreadLocalCommunicator::Get();
    ICommunicator* ptr = e->comm->GetCommunicator(name);
    if (ptr == nullptr) {
        return &default_comm;
    } else {
        return ptr;
    }
}
// perform in-place allreduce, on sendrecvbuf
void Allreduce_(Buffer sendrecvbuf, ReduceFunction red, mpi::DataType dtype,
        mpi::OpType op, const std::string& name) {
    GetCommunicator(name)->Allreduce(sendrecvbuf, red);
}

}  // namespace comm
}  // namespace rdc
