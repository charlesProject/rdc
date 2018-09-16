#include <cstring>
#include "transport/tcp/tcpadapter.h"
#ifdef RDC_USE_RDMA
#include "transport/rdma/rdma_adapter.h"
#endif
#include "core/env.h"
#include "utils/utils.h"

namespace rdc {
IAdapter* GetAdapter() {
    const char* backend = Env::Get()->Find("RDC_BACKEND");
    if (std::strncmp(backend, "TCP", 3) == 0) {
        static TcpAdapter adapter;
        return &adapter;
    }
#ifdef RDC_USE_RDMA
    if (std::strncmp(backend, "RDMA", 4) == 0) {
        static RdmaAdapter adapter;
        return &adapter;
    }
#endif
    if (backend == nullptr) {
        static TcpAdapter adapter;
        return &adapter;
    }
}
}
