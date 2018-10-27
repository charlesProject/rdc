// Copyright by Contributors
// implementations in ctypes
#include <cstring>
#include <string>
#define LOGGING_IMPLEMENTATION 1
#include "c_api/c_api.h"
#include "rdc.h"

namespace rdc {
namespace c_api {
// helper use to avoid BitOR operator
template <typename OP, typename DType>
struct AllreduceFloat {
    inline static void Allreduce(DType *senrecvbuf_, size_t count) {
        rdc::Allreduce<OP>(senrecvbuf_, count);
    }
};

template <typename DType>
struct AllreduceFloat<op::BitOR, DType> {
    inline static void Allreduce(DType *senrecvbuf_, size_t count) {
        LOG_F(ERROR, "DataType does not support bitwise or operation");
    }
};

template <typename OP>
inline void Allreduce_(void *sendrecvbuf_, size_t count,
                       mpi::DataType enum_dtype) {
    using namespace mpi;
    switch (enum_dtype) {
        case kChar:
            rdc::Allreduce<OP>(static_cast<char *>(sendrecvbuf_), count);
            return;
        case kUChar:
            rdc::Allreduce<OP>(static_cast<unsigned char *>(sendrecvbuf_),
                               count);
            return;
        case kInt:
            rdc::Allreduce<OP>(static_cast<int *>(sendrecvbuf_), count);
            return;
        case kUInt:
            rdc::Allreduce<OP>(static_cast<unsigned *>(sendrecvbuf_), count);
            return;
        case kLong:
            rdc::Allreduce<OP>(static_cast<long *>(sendrecvbuf_),  // NOLINT(*)
                               count);
            return;
        case kULong:
            rdc::Allreduce<OP>(
                static_cast<unsigned long *>(sendrecvbuf_),  // NOLINT(*)
                count);
            return;
        case kFloat:
            AllreduceFloat<OP, float>::Allreduce(
                static_cast<float *>(sendrecvbuf_), count);
            return;
        case kDouble:
            AllreduceFloat<OP, double>::Allreduce(
                static_cast<double *>(sendrecvbuf_), count);
            return;
        default:
            LOG_F(ERROR, "unknown data_type");
    }
}
inline void Allreduce(void *sendrecvbuf, size_t count, mpi::DataType enum_dtype,
                      mpi::OpType enum_op) {
    using namespace mpi;
    switch (enum_op) {
        case kMax:
            Allreduce_<op::Max>(sendrecvbuf, count, enum_dtype);
            return;
        case kMin:
            Allreduce_<op::Min>(sendrecvbuf, count, enum_dtype);
            return;
        case kSum:
            Allreduce_<op::Sum>(sendrecvbuf, count, enum_dtype);
            return;
        case kBitwiseOR:
            Allreduce_<op::BitOR>(sendrecvbuf, count, enum_dtype);
            return;
        default:
            LOG_F(ERROR, "unknown enum_op");
    }
}

// wrapper for serialization
struct ReadWrapper : public Serializable {
    std::string *p_str;
    explicit ReadWrapper(std::string *p_str) : p_str(p_str) {}
    virtual void Load(Stream *fi) {
        uint64_t sz;
        CHECK_F(fi->Read(&sz, sizeof(sz)) != 0, "Read pickle string");
        p_str->resize(sz);
        if (sz != 0) {
            CHECK_F(fi->Read(&(*p_str)[0], sizeof(char) * sz) != 0,
                    "Read pickle string");
        }
    }
    virtual void Save(Stream *fo) const { LOG_F(ERROR, "not implemented"); }
};

struct WriteWrapper : public Serializable {
    const char *data;
    size_t length;
    explicit WriteWrapper(const char *data, size_t length)
        : data(data), length(length) {}
    virtual void Load(Stream *fi) { LOG_F(ERROR, "not implemented"); }
    virtual void Save(Stream *fo) const {
        uint64_t sz = static_cast<uint16_t>(length);
        fo->Write(&sz, sizeof(sz));
        fo->Write(data, length * sizeof(char));
    }
};
}  // namespace c_api
}  // namespace rdc

void RdcInit(int argc, char *argv[]) { rdc::Init(argc, argv); }

void RdcFinalize() { rdc::Finalize(); }

int RdcGetRank() { return rdc::GetRank(); }

int RdcGetWorldSize() { return rdc::GetWorldSize(); }

int RdcIsDistributed() { return rdc::IsDistributed(); }

void RdcTrackerPrint(const char *msg) {
    std::string m(msg);
    rdc::TrackerPrint(m);
}

void RdcGetProcessorName(char *out_name, rdc_ulong *out_len,
                         rdc_ulong max_len) {
    std::string s = rdc::GetProcessorName();
    if (s.length() > max_len) {
        s.resize(max_len - 1);
    }
    strcpy(out_name, s.c_str());  // NOLINT(*)
    *out_len = static_cast<rdc_ulong>(s.length());
}

void RdcBroadcast(void *sendrecv_data, rdc_ulong size, int root) {
    rdc::Broadcast(sendrecv_data, size, root);
}

void RdcAllreduce(void *sendrecvbuf, size_t count, int enum_dtype,
                  int enum_op) {
    rdc::c_api::Allreduce(sendrecvbuf, count,
                          static_cast<rdc::mpi::DataType>(enum_dtype),
                          static_cast<rdc::mpi::OpType>(enum_op));
}

int RdcLoadCheckPoint(char **out_global_model, rdc_ulong *out_global_len,
                      char **out_local_model, rdc_ulong *out_local_len) {
    // NOTE: this function is not thread-safe
    using rdc::utils::BeginPtr;
    using namespace rdc::c_api;  // NOLINT(*)
    static std::string global_buffer;
    static std::string local_buffer;

    ReadWrapper sg(&global_buffer);
    ReadWrapper sl(&local_buffer);
    int version;

    if (out_local_model == NULL) {
        version = rdc::LoadCheckPoint(&sg, NULL);
        *out_global_model = BeginPtr(global_buffer);
        *out_global_len = static_cast<rdc_ulong>(global_buffer.length());
    } else {
        version = rdc::LoadCheckPoint(&sg, &sl);
        *out_global_model = BeginPtr(global_buffer);
        *out_global_len = static_cast<rdc_ulong>(global_buffer.length());
        *out_local_model = BeginPtr(local_buffer);
        *out_local_len = static_cast<rdc_ulong>(local_buffer.length());
    }
    return version;
}

void RdcCheckPoint(const char *global_model, rdc_ulong global_len,
                   const char *local_model, rdc_ulong local_len) {
    using namespace rdc::c_api;  // NOLINT(*)
    WriteWrapper sg(global_model, global_len);
    WriteWrapper sl(local_model, local_len);
    if (local_model == nullptr) {
        rdc::CheckPoint(&sg, nullptr);
    } else {
        rdc::CheckPoint(&sg, &sl);
    }
}

int RdcVersionNumber() { return rdc::VersionNumber(); }

int RdcLinkTag() { return 0; }
