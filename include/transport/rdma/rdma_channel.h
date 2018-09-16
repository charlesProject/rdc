#pragma once
#include <infiniband/verbs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include "core/work_request.h"
#include "transport/channel.h"
#include "core/status.h"
#include "utils/utils.h"

namespace rdc {
const int kNumCompQueueEntries = 100;
const uint64_t kBufSize = 1 << 10U;

struct __attribute__ ((packed)) RdmaAddr {
    uint32_t lid;
    uint32_t qpn;
    uint32_t psn;
    uint32_t rkey;
    uint64_t snp;
    uint64_t iid;
    uint64_t raddr;
};

inline std::string PrintAddr(RdmaAddr addr) {
    std::string addr_str;
    addr_str += std::to_string(addr.lid);
    addr_str += "\t" + std::to_string(addr.qpn);
    addr_str += "\t" + std::to_string(addr.psn);
    addr_str += "\t" + std::to_string(addr.snp);
    addr_str += "\t" + std::to_string(addr.iid);
    return addr_str;
}

class RdmaAdapter;
struct RdmaChannel : IChannel {
    RdmaChannel();
    RdmaChannel(RdmaAdapter* adapter, uint64_t buf_size);
    RdmaChannel(RdmaAdapter* adapter);
    ~RdmaChannel() {
        delete[] send_buf_;
        delete[] recv_buf_;
    }
    WorkCompletion ISend(const void* sendbuf, size_t size) override;
    WorkCompletion IRecv(void* recvbuf, size_t size) override;
    Status Connect(const std::string& hostname, const uint32_t& port) override;
    void Close() override {
        return this->ExitRdmaContext();
    }
    void set_peer_addr(const RdmaAddr& peer_addr) {
        peer_addr_ = peer_addr;
    }
    RdmaAddr addr() const {
        return self_addr_;
    }

    RdmaAddr peer_addr() const {
        return peer_addr_;
    }
    void AfterConnection();
protected:
    void InitRdmaContext();
    void ExitRdmaContext();
    void CreateQueuePair();
    void CreateLocalAddr();
    void InitQueuePair();
    void EnableQueuePairForSend();
    void EnableQueuePairForRecv();
    uint8_t* send_buf_;
    uint8_t* recv_buf_;
    uint64_t buf_size_;
    RdmaAddr self_addr_;
    RdmaAddr peer_addr_;
    ibv_qp* queue_pair_;
    ibv_mr* send_memory_region_;
    ibv_mr* recv_memory_region_;
    RdmaAdapter* adapter_;
    int sgid_idx_;
    int gid_idx_;
    int num_comp_queue_entries_;
};
} // namespace rdc
