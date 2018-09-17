#ifdef RDC_USE_RDMA
#include "transport/rdma/rdma_adapter.h"
#include "transport/rdma/rdma_utils.h"
#include "core/threadpool.h"
#include "core/logging.h"

namespace rdc {

static const uint32_t kConcurrentOps = 4;

void RdmaAdapter::InitContext() {
    GetAvaliableDeviceAndPort(dev_, ib_port_);
    CHECK_NOTNULL(context_ = ibv_open_device(dev_));
    CHECK_NOTNULL(protection_domain_ = ibv_alloc_pd(context_));

    auto ret = ibv_query_device(context_, &dev_attr_);
    
    CHECK_NOTNULL(completion_queue_ = ibv_create_cq(context_,
        dev_attr_.max_cqe, context_, nullptr, 0));
    sgid_idx_ = roce::GetGid(ib_port_, context_);
    int rc = ibv_query_gid(context_, ib_port_, gid_idx_, &gid_);
    snp_ = gid_.global.subnet_prefix;
    iid_ = gid_.global.interface_id;
    srand(time(0));
}
void RdmaAdapter::ExitContext() {
    CHECK_EQ(ibv_close_device(context_), 0);
    CHECK_EQ(ibv_destroy_cq(completion_queue_), 0);
    CHECK_EQ(ibv_dealloc_pd(protection_domain_), 0);
}
void RdmaAdapter::PollForever() {
    while(!ready()) {}
    for (;;) {
        ibv_wc wcs[kConcurrentOps];
        int num_succeeded = 0;
        do {
            if (finished()) {
                return;
            }
            num_succeeded = ibv_poll_cq(completion_queue_, kConcurrentOps, wcs);
        } while(num_succeeded == 0);
        CHECK_GE_F(num_succeeded, 0, "poll CQ failed");
        for (auto i = 0U; i < num_succeeded; i++) {
            auto wc = wcs[i];
            CHECK_EQ_F(wc.status, IBV_WC_SUCCESS,
                       "%s %d", ibv_wc_status_str(wc.status),wc.status);
            auto& work_req = WorkRequestManager::Get()->
                            GetWorkRequest(wc.wr_id);
            size_t len = 0;
            if (work_req.work_type() == kRecv) {
                len = wc.byte_len;
            } else {
                len = work_req.nbytes();
            }
            if (work_req.AddBytes(len)) {
                if (work_req.work_type() == kRecv) {
                    std::memcpy(work_req.ptr(), work_req.extra_data(),
                        work_req.nbytes());
                }
                work_req.Notify();
            }
        }
    }
}

int RdmaAdapter::Listen(const uint32_t& tcp_port) {
    struct sockaddr_in owned_addr;
    std::memset(&owned_addr, 0 , sizeof(owned_addr));
    owned_addr.sin_family = AF_INET;
    owned_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    owned_addr.sin_port = htons(tcp_port);
    if(bind(this->listen_fd_, (struct sockaddr *) &owned_addr,
            sizeof(owned_addr)) != 0) {
        LOG_F(ERROR, "Fail to listen on port %d : %s " ,
            tcp_port , strerror(errno));
    };
    int32_t opt = 0;
    setsockopt(this->listen_fd_, SOL_SOCKET, SO_REUSEPORT,
               &opt, sizeof(opt));
    if (listen(this->listen_fd_, kNumBacklogs) != 0) {
        LOG_F(ERROR, "Fail to listen on port %d : %s " ,
            tcp_port , strerror(errno));
    }
    return 0;
}


IChannel* RdmaAdapter::Accept() {
    // accept the connection$
    sockaddr_in incoming_addr;
    socklen_t incoming_addr_len = sizeof(incoming_addr);
    int32_t accepted_fd = accept(this->listen_fd_,
                                 (struct sockaddr*)&incoming_addr,
                                 &incoming_addr_len);
    CHECK_GE(accepted_fd, 0);

   // set flags to check
    auto channel = new RdmaChannel(this);

    RdmaAddr peer_addr;
    CHECK_EQ_F(recv(accepted_fd , (char*)&peer_addr,
              sizeof(peer_addr), 0), sizeof(peer_addr),
              "Could not receive local address to peer");
    channel->set_peer_addr(peer_addr);
    auto owned_addr = channel->addr();
    CHECK_EQ(send(accepted_fd , (char*)&owned_addr,
             sizeof(owned_addr), 0), sizeof(owned_addr))
             << "Could not send local address to peer";
    channel->AfterConnection();
    close(accepted_fd);
    return channel;
}
}  // namespace rdc
#endif
