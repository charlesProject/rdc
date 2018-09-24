#pragma once
#ifdef RDC_USE_RDMA
#include <infiniband/verbs.h>
#include "transport/rdma/rdma_memory_mgr.h"
#endif
#include "core/env.h"
namespace rdc {
class Buffer {
public:
    Buffer(void* addr, uint64_t size_in_bytes):
        Buffer(addr, size_in_bytes, false) {}
    Buffer(const void* addr, uint64_t size_in_bytes):
        Buffer(addr, size_in_bytes, false) {}
    Buffer(void* addr, uint64_t size_in_bytes, const bool& pinned):
        addr_(addr), size_in_bytes_(size_in_bytes),
        is_mutable_(true), pinned_(pinned) {
        bool use_rdma = Env::Get()->GetEnv("RDC_USE_RDMA", 0);
#ifdef RDC_USE_RDMA
        if (pinned && use_rdma) {
            memory_region_ = RdmaMemoryMgr::Get()->InsertMemoryRegion(
                    addr_, size_in_bytes);
        }
#endif
    }
    Buffer(const void* addr, uint64_t size_in_bytes, const bool& pinned):
        addr_(const_cast<void*>(addr)), size_in_bytes_(size_in_bytes),
        is_mutable_(false), pinned_(pinned) {
        bool use_rdma = Env::Get()->GetEnv("RDC_USE_RDMA", 0);
#ifdef RDC_USE_RDMA
        if (pinned && use_rdma) {
            memory_region_ = RdmaMemoryMgr::Get()->InsertMemoryRegion(
                    addr_, size_in_bytes);
        }
#endif
    }
    void* addr() const {
        return addr_;
    }
    uint64_t size_in_bytes() const {
        return size_in_bytes_;
    }
    bool is_mutable() const {
        return is_mutable_;
    }
#ifdef RDC_USE_RDMA
    ibv_mr* memory_region() const {
        return memory_region_;
    }
#endif
private:
    void* addr_;
    uint64_t size_in_bytes_;
    bool is_mutable_;
    bool pinned_;
#ifdef RDC_USE_RDMA
    ibv_mr* memory_region_;
#endif
};
}  // namesapce rdc
