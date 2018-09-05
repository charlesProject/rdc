/*!
 * Copyright by Contributors
 * \file rdc-inl.h
 * \brief implementation of inline template function for rdc interface
 *
 * \author Ankun Zheng
 */
#pragma once
namespace rdc {
namespace comm {
namespace mpi {
// template function to translate type to enum indicator
template<typename DType>
inline DataType GetType(void);
template<>
inline DataType GetType<char>(void) {
    return kChar;
}
template<>
inline DataType GetType<unsigned char>(void) {
    return kUChar;
}
template<>
inline DataType GetType<int>(void) {
    return kInt;
}
template<>
inline DataType GetType<unsigned int>(void) { // NOLINT(*)
    return kUInt;
}
template<>
inline DataType GetType<long>(void) {  // NOLINT(*)
    return kLong;
}
template<>
inline DataType GetType<unsigned long>(void) { // NOLINT(*)
    return kULong;
}
template<>
inline DataType GetType<float>(void) {
    return kFloat;
}
template<>
inline DataType GetType<double>(void) {
    return kDouble;
}
template<>
inline DataType GetType<long long>(void) { // NOLINT(*)
    return kLongLong;
}
template<>
inline DataType GetType<unsigned long long>(void) { // NOLINT(*)
    return kULongLong;
}
}  // namespace mpi
}  // namespace comm

namespace op {
struct Max {
    static const comm::mpi::OpType kType = comm::mpi::kMax;
    template<typename DType>
    inline static void Reduce(DType &dst, const DType &src) { // NOLINT(*)
        if (dst < src) dst = src;
    }
};
struct Min {
    static const comm::mpi::OpType kType = comm::mpi::kMin;
    template<typename DType>
    inline static void Reduce(DType &dst, const DType &src) { // NOLINT(*)
        if (dst > src) dst = src;
    }
};
struct Sum {
    static const comm::mpi::OpType kType = comm::mpi::kSum;
    template<typename DType>
    inline static void Reduce(DType &dst, const DType &src) { // NOLINT(*)
        dst += src;
    }
};
struct BitOR {
    static const comm::mpi::OpType kType = comm::mpi::kBitwiseOR;
    template<typename DType>
    inline static void Reduce(DType &dst, const DType &src) { // NOLINT(*)
        dst |= src;
    }
};
template<typename OP, typename DType>
inline void Reducer(const void *src_, void *dst_, int len, const MPI::Datatype &dtype) {
    const DType *src = (const DType*)src_;
    DType *dst = (DType*)dst_;  // NOLINT(*)
    for (int i = 0; i < len; ++i) {
        OP::Reduce(dst[i], src[i]);
    }
}
}  // namespace op
}  // namespace  rdc
