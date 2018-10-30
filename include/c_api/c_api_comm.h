#pragma once
#include "c_api_common.h"
/*All custom type handle are just void*,
 * it may be allocated in c iterface and deallocated when python class finish
 * its lifetime
 * */
typedef void *WorkCompletionHandle;
typedef void *ChainWorkCompletionHandle;
typedef void *BufferHandle;
typedef void *ICommunicatorHandle;
/*! \brief Inhibit C++ name-mangling for Rdc functions. */
#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/*!/brief Create a new buffer for communication
 * /param buf handle for the buffer to be created
 * /param addr address the buffer created from
 * /param size_in_bytes size of the buffer in bytes
 * /param pinned whether this buffer shounld be pinned, so will not be swapout
 * */
RDC_DLL void RdcNewBuffer(BufferHandle *buf, void *addr,
                          rdc_ulong size_in_bytes, bool pinned = false);

/*!/brief Delelete a created bufffer
 *  /param buf handlle for the created buffer
 * */
RDC_DLL void RdcDelBuffer(BufferHandle buf);

/*!/brief Deleted a created work completion
 * /param work_comp handle for the created work completion*
 * */
RDC_DLL void RdcDelWorkCompletion(WorkCompletionHandle work_comp);
/*!/brief Create a new communicator
 * /param comm_name name for this communicator
 * */
RDC_DLL void RdcNewCommunicator(ICommunicatorHandle *comm,
                                const char *comm_name);
/*!/brief Get a exsited communicator
 * /param comm_name name for this communicator
 * */
RDC_DLL void RdcGetCommunicator(ICommunicatorHandle *comm,
                                const char *comm_name);

/*!/brief nonblocking send
 * /param work_comp handle for work completion this function will return
 * /param comm communicator object
 * /param buf buffer will be sent
 * /param dest_rank rank of destination process
 * */
RDC_DLL void RdcISend(WorkCompletionHandle *work_comp, ICommunicatorHandle comm,
                      BufferHandle buf, int dest_rank);
/*!/brief nonblocking send
 * /param work_comp handle for work completion this function will return
 * /param comm communicator object
 * /param buf buffer will be sent
 * /param source_rank rank of source process
 * */

RDC_DLL void RdcIRecv(WorkCompletionHandle *work_comp, ICommunicatorHandle comm,
                      BufferHandle buf, int src_rank);
#ifdef __cplusplus
}
#endif  // __cplusplus
