/*!
 *  Copyright (c) 2018 by Contributors
 * \file allreduce_base.h
 * \brief Basic implementation of communication primitives
 *   include non-block send recv, allreduce, broadcast and allgather
 *   using TCP non-block socket or RDMA for communication.
 *
 *   This implementation provides robust version of Communication Primitives
 *   by considering node failure
 *
 * \author Ankun Zheng
 */
#pragma once

#include "comm/communicator_base.h"
#include "transport/buffer.h"

namespace rdc {
/*!
 \brief model struct
*/
struct Model {
    std::uint64_t version_number;
    std::vector<Buffer> checkpoint_in_mem;
    std::string checkpoint_filename;
};


class CommunicatorRobust : public Communicator {
public:

private:
    Model global_ckpt_;
    std::vector<Model> local_ckpts_;
};
}
