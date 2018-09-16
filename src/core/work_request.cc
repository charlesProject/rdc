#include "core/work_request.h"
namespace rdc {
bool WorkRequest::AddBytes(const size_t nbytes) {
    completed_bytes_ += nbytes;
    if (completed_bytes_ == size_in_bytes_) {
        status_ = Status::kSuccess;
        //done_ = true;
        return true;
    }
    return false;
}
}
