#pragma once
#include <tuple>
#include <string>
#include "utils/string_utils.h"
#include "core/logging.h"
namespace rdc {


const uint32_t kNumBacklogs = 128;

enum Backend {
    kTcp = 0,
    kRdma = 2,
    kIpc = 3,
};
/* !\brief parse a address before connecting, a valid address is represented
 * as a tuple (backend, host, port) and will be represented as a string like
 * "tcp:127.0.0.1:8000"*/
inline std::tuple<std::string, std::string, uint32_t> ParseAddress(
        const std::string& addr_str) {
    auto addr_parts = str_utils::Split(addr_str, ':');
    std::string backend = addr_parts[0];
    std::string host = addr_parts[1];
    uint32_t port = std::atoi(addr_parts[2].c_str());
    return std::make_tuple(backend, host, port);
}

inline std::tuple<Backend, std::string, uint32_t> ParseAddr(
        const std::string& addr_str) {
    std::string backend_str;
    std::string host;
    uint32_t port;
    std::tie(backend_str, host, port) = ParseAddress(addr_str);
    Backend backend;
    if (backend_str == "tcp") {
        backend = kTcp;
    } else if (backend_str == "rdma") {
        backend = kRdma;
    } else if (backend_str == "ipc"){
        backend = kIpc;
    } else {
        backend = kTcp;
    }
    return std::make_tuple(backend, host, port);
}

inline std::string GetBackendString(const Backend& backend) {
    std::string backend_str;
    if (backend == kTcp) {
        backend_str = "tcp";
    } else if (backend == kRdma) {
        backend_str = "rdma";
    } else if (backend == kIpc) {
        backend_str = "ipc";
    } else {
        backend_str = "tcp";
    }
    return backend_str;
}

class IChannel;
class IAdapter {
public:
    virtual IChannel* Accept() = 0;
    virtual void Listen(const int& port) = 0;
    void set_backend(const Backend& backend) {
        backend_ = backend;
    }
    Backend backend() const {
        return backend_;
    }
    std::string backend_str() const {
        return GetBackendString(backend_);
    }
private:
    Backend backend_;
};

IAdapter* GetAdapter();
} // namespace rdc
