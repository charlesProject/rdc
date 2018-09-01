#include "core/work_request.h"
namespace rabit {
bool WorkRequest::AddBytes(const size_t nbytes) {
    //completed_bytes_.fetch_add(nbytes, std::memory_order_release);
    completed_bytes_ += nbytes;
    if (completed_bytes_ == size_) {
        status_ = Status::kSuccess;
        done_ = true;
        //WorkRequestManager::Get()->set_status(req_id_, Status::kSuccess);
        //WorkRequestManager::Get()->set_done(req_id_, true);
        return true;
    }
    return false;
}
}
