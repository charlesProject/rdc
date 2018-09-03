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
//typedef CommunicatorRobust WorldComm;
//#else
typedef Communicator WorldComm;
//#endif

/*! \brief entry to to easily hold returning information */
struct ThreadLocalEntry {
    /*! \brief stores the current comm */
    std::unique_ptr<WorldComm> comm;
    /*! \brief whether init has been called */
    bool initialized;
    /*! \brief constructor */
    ThreadLocalEntry() : initialized(false) {}
};

// define the threadlocal store.
typedef ThreadLocalStore<ThreadLocalEntry> EngineThreadLocal;

/*! \brief intiialize the synchronization module */
void Init(int argc, char *argv[]) {
    ThreadLocalEntry* e = EngineThreadLocal::Get();
    utils::Check(e->comm.get() == nullptr,
                 "rdc::Init is already called in this thread");
    e->initialized = true;
    e->comm.reset(new WorldComm());
    e->comm->Init(argc, argv);
}

/*! \brief finalize syncrhonization module */
void Finalize() {
    ThreadLocalEntry* e = EngineThreadLocal::Get();
    utils::Check(e->comm.get() != nullptr,
                 "rdc::Finalize comm is not initialized \
                 or already been finalized.");
    e->comm->Shutdown();
    e->comm.reset(nullptr);
}

/*! \brief singleton method to get comm */
ICommunicator *GetEngine() {
  // un-initialized default manager.
  static Communicator default_manager;
  ThreadLocalEntry* e = EngineThreadLocal::Get();
  ICommunicator* ptr = e->comm.get();
  if (ptr == nullptr) {
    utils::Check(!e->initialized,
                 "Doing rdc call after Finalize");
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
    GetEngine()->Allreduce(sendrecvbuf, type_nbytes, count,
                           red);
}

}  // namespace comm
}  // namespace rdc
