#pragma once
#include <string>
#include "utils/string_utils.h"
#include "core/logging.h"

namespace rdc {
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
class IChannel;
class IAdapter {
    virtual IChannel* Accept() = 0;
};

} // namespace rdc
