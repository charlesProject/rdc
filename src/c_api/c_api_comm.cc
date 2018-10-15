#include "c_api/c_api_comm.h"
#include "comm/communicator.h"
#include "core/work_request.h"
#include "rdc.h"
#include "transport/buffer.h"
void RdcNewBuffer(BufferHandle* buffer, void* addr, rdc_ulong size_in_bytes,
                  bool pinned) {
    *buffer = static_cast<BufferHandle>(
        rdc::Buffer::New(addr, size_in_bytes, pinned));
}
void RdcDelBuffer(BufferHandle buffer) { rdc::Buffer::Delete(buffer); }
void RdcNewCommunicator(ICommunicatorHandle comm, const char* comm_name) {
    comm = static_cast<ICommunicatorHandle>(rdc::NewCommunicator(comm_name));
}
void RdcISend(WorkCompletionHandle* work_comp, ICommunicatorHandle comm,
              BufferHandle buf, int dest_rank) {
    *work_comp = static_cast<WorkCompletionHandle>(
        static_cast<rdc::comm::ICommunicator*>(comm)->ISend(
            *static_cast<rdc::Buffer*>(buf), dest_rank));
}
void RdcIRecv(WorkCompletionHandle* work_comp, ICommunicatorHandle comm,
              BufferHandle buf, int src_rank) {
    *work_comp = static_cast<WorkCompletionHandle>(
        static_cast<rdc::comm::ICommunicator*>(comm)->IRecv(
            *static_cast<rdc::Buffer*>(buf), src_rank));
}
