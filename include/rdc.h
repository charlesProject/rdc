/*!
 *  Copyright (c) 2018 by Contributors
 * \file rdc.h
 * \brief This file defines rdc's Allreduce/Broadcast interface
 *   The rdc engine contains the actual implementation
 *   Code that only uses this header can also be compiled with MPI Allreduce (non fault-tolerant),
 *
 *   rdc.h and serializable.h is all what the user needs to use the rdc interface
 * \author Ankun Zheng
 */
#pragma once  // NOLINT(*)
#include <string>
#include <vector>


// optionally support of lambda functions in C++11, if available
#include <functional>
// comminicator definition of rdc, defines internal implementation
// to use rdc interface, there is no need to read engine.h
// rdc.h and serializable.h are enough to use the interface
#include "comm/communicator.h"
#include "core/work_request.h"
/*! \brief rdc namespace */
namespace rdc {
/*!
 * \brief defines stream used in rdc
 * see definition of Stream in dmlc/io.h
 */
typedef dmlc::Stream Stream;
/*!
 * \brief defines serializable objects used in rdc
 * see definition of Serializable in dmlc/io.h
 */
typedef dmlc::Serializable Serializable;

/*!
 * \brief reduction operators namespace
 */
namespace op {
/*!
 * \class rdc::op::Max
 * \brief maximum reduction operator
 */
struct Max;
/*!
 * \class rdc::op::Min
 * \brief minimum reduction operator
 */
struct Min;
/*!
 * \class rdc::op::Sum
 * \brief sum reduction operator
 */
struct Sum;
/*!
 * \class rdc::op::BitOR
 * \brief bitwise OR reduction operator
 */
struct BitOR;
}  // namespace op
/*!
 * \brief initializes rdc, call this once at the beginning of your program
 * \param argc number of arguments in argv
 * \param argv the array of input arguments
 */
inline void Init(int argc, char *argv[]);
inline void NewCommunicator(const std::string& name);
/*!
 * \brief finalizes the rdc engine, call this function after you finished with all the jobs
 */
inline void Finalize();
/*! \brief gets rank of the current process */
inline int GetRank();
/*! \brief gets total number of processes */
inline int GetWorldSize();
/*! \brief whether rdc env is in distributed mode */
inline bool IsDistributed();

/*! \brief gets processor's name */
inline std::string GetProcessorName();
/*!
 * \brief prints the msg to the tracker,
 *    this function can be used to communicate progress information to
 *    the user who monitors the tracker
 * \param msg the message to be printed
 */
inline void TrackerPrint(const std::string &msg);
inline void Send(void *send_data, size_t size, int dest);
inline void Recv(void *recv_data, size_t size, int src);
inline void Barrier();
/*!
 * \brief broadcasts a memory region to every node from the root
 *
 *     Example: int a = 1; Broadcast(&a, sizeof(a), root);
 * \param sendrecv_data the pointer to the send/receive buffer,
 * \param size the data size
 * \param root the process root
 */
inline void Broadcast(void *sendrecv_data, size_t size, int root,
        const std::string& comm_name = kWorldCommName);
/*!
 * \brief broadcasts an std::vector<DType> to every node from root
 * \param sendrecv_data the send/receive vector,
 *        for the receiver, the vector does not need to be pre-allocated
 * \param root the process root
 * \tparam DType the data type stored in the vector, has to be a simple data type
 *               that can be directly transmitted by sending the sizeof(DType)
 */
template<typename DType>
inline void Broadcast(std::vector<DType>& sendrecv_data, int root,
        const std::string& comm_name = kWorldCommName);
/*!
 * \brief broadcasts a std::string to every node from the root
 * \param sendrecv_data the the send/receive buffer,
 *        for the receiver, the vector does not need to be pre-allocated
 * \param root the process root
 */
inline void Broadcast(std::string& sendrecv_data, int root,
        const std::string& comm_name = kWorldCommName);

template<typename DType>
inline void Allgather(std::vector<std::vector<DType>>& sendrecv_data,
        const std::string& comm_name = kWorldCommName);

inline void Allgather(void** sendrecv_data, size_t type_nbyes, size_t* counts,
        const std::string& comm_name = kWorldCommName);
/*!
 * \brief performs in-place Allreduce, on sendrecvbuf
 *        with a prepare function specified by a lambda function
 * \param sendrecvbuf buffer for both sending and receiving data
 * \param count number of elements to be reduced
 * \tparam OP see namespace op, reduce operator
 * \tparam DType data type
 */
template<typename OP, typename DType>
inline void Allreduce(DType *sendrecvbuf, size_t count,
        const std::string& comm_name = kWorldCommName);

/*!
 * \brief loads the latest check point
 * \param global_model pointer to the globally shared model/state
 *   when calling this function, the caller needs to guarantee that the global_model
 *   is the same in every node
 * \param local_model pointer to the local model that is specific to the current node/rank
 *   this can be NULL when no local model is needed
 *
 * \return the version number of the check point loaded
 *     if returned version == 0, this means no model has been CheckPointed
 *     the p_model is not touched, users should do the necessary initialization by themselves
 *
 * \code{.cpp}
 * // Example usage code of LoadCheckPoint
 * int iter = rdc::LoadCheckPoint(&model);
 * if (iter == 0) model.InitParameters();
 * for (i = iter; i < max_iter; ++i) {
 *   // do many things, include allreduce
 *   rdc::CheckPoint(model);
 * }
 * \endcode
 * \sa CheckPoint, VersionNumber
 */
inline int LoadCheckPoint(Serializable *global_model,
                          Serializable *local_model = NULL);
/*!
 * \brief checkpoints the model, meaning a stage of execution has finished.
 *  every time we call check point, a version number will be increased by one
 *
 * \param global_model pointer to the globally shared model/state
 *   when calling this function, the caller needs to guarantee that the global_model
 *   is the same in every node
 * \param local_model pointer to the local model that is specific to the current node/rank
 *   this can be NULL when no local state is needed
   * NOTE: local_model requires explicit replication of the model for fault-tolerance, which will
   *       bring replication cost in the CheckPoint function. global_model does not need explicit replication.
   *       So, only CheckPoint with the global_model if possible
   * \sa LoadCheckPoint, VersionNumber
   */
inline void CheckPoint(const Serializable *global_model,
                       const Serializable *local_model = NULL);
/*!
 * \brief This function can be used to replace CheckPoint for global_model only,
 *   when certain condition is met (see detailed explanation).
 *
 *   This is a "lazy" checkpoint such that only the pointer to the global_model is
 *   remembered and no memory copy is taken. To use this function, the user MUST ensure that:
 *   The global_model must remain unchanged until the last call of Allreduce/Broadcast in the current version finishes.
 *   In other words, the global_model model can be changed only between the last call of
 *   Allreduce/Broadcast and LazyCheckPoint, both in the same version
 *
 *   For example, suppose the calling sequence is:
 *   LazyCheckPoint, code1, Allreduce, code2, Broadcast, code3, LazyCheckPoint/(or can be CheckPoint)
 *
 *   Then the user MUST only change the global_model in code3.
 *
 *   The use of LazyCheckPoint instead of CheckPoint will improve the efficiency of the program.
 * \param global_model pointer to the globally shared model/state
 *   when calling this function, the caller needs to guarantee that the global_model
 *   is the same in every node
 * \sa LoadCheckPoint, CheckPoint, VersionNumber
 */
inline void LazyCheckPoint(const Serializable *global_model);
/*!
 * \return version number of the current stored model,
 *         which means how many calls to CheckPoint we made so far
 * \sa LoadCheckPoint, CheckPoint
 */
inline int VersionNumber();


inline std::unique_ptr<comm::ICommunicator> CreateGroup(
    const std::vector<int>& ranks,
    const std::string& group_name = "");
// ----- extensions that allow customized reducer ------
/*!
 * \brief template class to make customized reduce and all reduce easy
 *  Do not use reducer directly in the function you call Finalize,
 *   because the destructor can execute after Finalize
 * \tparam DType data type that to be reduced
 * \tparam freduce the customized reduction function
 *  DType must be a struct, with no pointer
 */
template<typename DType, std::function<
         void(DType &dst, const DType &src)> freduce>  // NOLINT(*)
class Reducer {
public:
    Reducer();
    /*!
     * \brief customized in-place all reduce operation
     * \param sendrecvbuf the in place send-recv buffer
     * \param count number of elements to be reduced
     */
    inline void Allreduce(DType *sendrecvbuf, size_t count);
};
/*!
 * \brief template class to make customized reduce,
 *  this class defines complex reducer handles all the data structure that can be
 *  serialized/deserialized into fixed size buffer
 *  Do not use reducer directly in the function you call Finalize, because the destructor can execute after Finalize
 *
 * \tparam DType data type that to be reduced, DType must contain the following functions:
 * \tparam freduce the customized reduction function
 *   (1) Save(IStream &fs)  (2) Load(IStream &fs) (3) Reduce(const DType &src, size_t max_nbyte)
 */
template<typename DType>
class SerializeReducer {
public:
    SerializeReducer();
    /*!
     * \brief customized in-place all reduce operation
     * \param sendrecvobj pointer to the array of objects to be reduced
     * \param max_nbyte maximum amount of memory needed to serialize each object
     *        this includes budget limit for intermediate and final result
     * \param count number of elements to be reduced
     */
    inline void Allreduce(DType *sendrecvobj,
                          size_t max_nbyte, size_t count);
private:
    /*! \brief temporal buffer used to do reduce*/
    std::string buffer_;
};
}  // namespace rdc
// implementation of template functions
#include "core/rdc-inl.h"