/*!
 * Copyright by Contributors
 * \file c_api.h
 * \author AnKun Zheng
 * \brief a C style API of rdc.
 */
#pragma once
#ifdef __cplusplus
#define RDC_EXTERN_C extern "C"
#include <cstdio>
#else
#define RDC_EXTERN_C
#include <stdio.h>
#endif

#if defined(_MSC_VER) || defined(_WIN32)
#define RDC_DLL RDC_EXTERN_C __declspec(dllexport)
#else
#define RDC_DLL RDC_EXTERN_C
#endif

/*! \brief rdc unsigned long type */
typedef unsigned long rbc_ulong;  // NOLINT(*)

/*!
 * \brief intialize the rdc module,
 *  call this once before using anything
 *  The additional arguments is not necessary.
 *  Usually rdc will detect settings
 *  from environment variables.
 * \param argc number of arguments in argv
 * \param argv the array of input arguments
 */
RDC_DLL void RdcInit(int argc, char *argv[]);

/*!
 * \brief finalize the rdc engine,
 * call this function after you finished all jobs.
 */
RDC_DLL void RdcFinalize(void);

/*! \brief get rank of current process */
RDC_DLL int RdcGetRank(void);

/*! \brief get total number of process */
RDC_DLL int RdcGetWorldSize(void);

/*! \brief get rank of current process */
RDC_DLL int RdcIsDistributed(void);

/*!
 * \brief print the msg to the tracker,
 *    this function can be used to communicate the information of the progress to
 *    the user who monitors the tracker
 * \param msg the message to be printed
 */
RDC_DLL void RdcTrackerPrint(const char *msg);
/*!
 * \brief get name of processor
 * \param out_name hold output string
 * \param out_len hold length of output string
 * \param max_len maximum buffer length of input
   */
RDC_DLL void RdcGetProcessorName(char *out_name,
                                     rbc_ulong *out_len,
                                     rbc_ulong max_len);
/*!
 * \brief broadcast an memory region to all others from root
 *
 *     Example: int a = 1; Broadcast(&a, sizeof(a), root);
 * \param sendrecv_data the pointer to send or recive buffer,
 * \param size the size of the data
 * \param root the root of process
 */
RDC_DLL void RdcBroadcast(void *sendrecv_data,
                              rbc_ulong size, int root);
/*!
 * \brief perform in-place allreduce, on sendrecvbuf
 *        this function is NOT thread-safe
 *
 * Example Usage: the following code gives sum of the result
 *     vector<int> data(10);
 *     ...
 *     Allreduce<op::Sum>(&data[0], data.size());
 *     ...
 * \param sendrecvbuf buffer for both sending and recving data
 * \param count number of elements to be reduced
 * \param enum_dtype the enumeration of data type, see rdc::engine::mpi::DataType in engine.h of rdc include
 * \param enum_op the enumeration of operation type, see rdc::engine::mpi::OpType in engine.h of rdc
 * \param prepare_fun Lazy preprocessing function, if it is not NULL, prepare_fun(prepare_arg)
 *                    will be called by the function before performing Allreduce, to intialize the data in sendrecvbuf_.
 *                     If the result of Allreduce can be recovered directly, then prepare_func will NOT be called
   * \param prepare_arg argument used to passed into the lazy preprocessing function
   */
RDC_DLL void RdcAllreduce(void *sendrecvbuf,
                              size_t count,
                              int enum_dtype,
                              int enum_op,
                              void (*prepare_fun)(void *arg),
                              void *prepare_arg);

/*!
 * \brief load latest check point
 * \param out_global_model hold output of serialized global_model
 * \param out_global_len the output length of serialized global model
 * \param out_local_model hold output of serialized local_model, can be NULL
 * \param out_local_len the output length of serialized local model, can be NULL
 *
 * \return the version number of check point loaded
 *     if returned version == 0, this means no model has been CheckPointed
 *     nothing will be touched
 */
RDC_DLL int RdcLoadCheckPoint(char **out_global_model,
                                  rbc_ulong *out_global_len,
                                  char **out_local_model,
                                  rbc_ulong *out_local_len);
/*!
 * \brief checkpoint the model, meaning we finished a stage of execution
 *  every time we call check point, there is a version number which will increase by one
 *
 * \param global_model hold content of serialized global_model
 * \param global_len the content length of serialized global model
 * \param local_model hold content of serialized local_model, can be NULL
 * \param local_len the content length of serialized local model, can be NULL
 *
 * NOTE: local_model requires explicit replication of the model for fault-tolerance, which will
 *       bring replication cost in CheckPoint function. global_model do not need explicit replication.
 *       So only CheckPoint with global_model if possible
 */
RDC_DLL void RdcCheckPoint(const char *global_model,
                               rbc_ulong global_len,
                               const char *local_model,
                               rbc_ulong local_len);
/*!
 * \return version number of current stored model,
 * which means how many calls to CheckPoint we made so far
 */
RDC_DLL int RdcVersionNumber(void);


/*!
 * \brief a Dummy function,
 *  used to cause force link of C API  into the  DLL.
 * \code
 * // force link rdc C API library.
 * static int must_link_rdc_ = RdcLinkTag();
 * \endcode
 * \return a dummy integer.
 */
RDC_DLL int RdcLinkTag(void);

