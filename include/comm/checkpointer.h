#pragma once

#include "comm/communicator_base.h"
#include "common/bitmask.h"
#include "common/pool.h"
#include "io/file_io.h"
#include "io/memory_io.h"
#include "transport/buffer.h"
#include "utils/topo_utils.h"

namespace rdc {
namespace comm {
/*!
 \brief model struct
*/
enum class CheckPointBehavior : uint32_t {
    InMemory = 0x01,
    OnDisk = 0x02,
    OnDevice = 0x04,
    _bitmask_max_element = OnDevice
};

BITMASK_DEFINE(CheckPointBehavior);

enum class ReplicaStrategy : uint32_t {
    WithTracker = 0x01,
    WithPeers = 0x02,
    NoReplica = 0x04,
    _bitmask_max_element = NoReplica
};

BITMASK_DEFINE(ReplicaStrategy);

enum class StateKind : uint32_t {
    Local = 0x01,
    Global = 0x02,
    Unkown = 0x04,
};

/**
 * @brief: state base class, state is in a description of a piece of current
 */
class BaseState {
public:
    BaseState() {
        checkpoint_behavior_ = 0;
    }
    ~BaseState() = default;
    BaseState(const std::vector<CheckPointBehavior>& checkpoint_behaviors)
        : BaseState() {
        for (auto&& checkpoint_behavior : checkpoint_behaviors) {
            checkpoint_behavior_ |= checkpoint_behavior;
        }
    }
    BaseState(const CheckPointBehavior& checkpoint_behavior,
              const std::string& filepath) {
        CHECK(checkpoint_behavior_ | CheckPointBehavior::OnDisk);
        filepath_ = filepath;
        checkpoint_behavior_ = checkpoint_behavior;
        on_disk_holder_.reset(new FileStream(filepath));
    }
    BaseState(const CheckPointBehavior& checkpoint_behavior, void* ptr,
              const size_t& size)
        : state_size_(size) {
        CHECK(checkpoint_behavior_ | ~CheckPointBehavior::OnDisk);
        checkpoint_behavior_ = checkpoint_behavior;
        if (checkpoint_behavior_ | CheckPointBehavior::InMemory) {
            in_memory_holder_.reset(new Buffer(ptr, size));
        } else if (checkpoint_behavior_ | CheckPointBehavior::OnDevice) {
            on_device_holder_.reset(new Buffer(ptr, size));
        }
    }
    /*---------------------------peoperties-----------------------------*/
    uint64_t version_number() const {
        return version_number_;
    }

    std::string state_name() const {
        return state_name_;
    }

    size_t state_size() const {
        return state_size_;
    }

    std::string filepath() const {
        return filepath_;
    }

    std::shared_ptr<Buffer> in_memory_holder() const {
        return in_memory_holder_;
    }

    std::shared_ptr<FileStream> on_disk_holder() const {
        return on_disk_holder_;
    }

    std::shared_ptr<Buffer> on_device_holder() const {
        return on_device_holder_;
    }

    bitmask::bitmask<CheckPointBehavior> checkpoint_behavior() const {
        return checkpoint_behavior_;
    }

private:
    uint64_t version_number_;
    std::string state_name_;
    size_t state_size_;
    bool enable_double_buffer_;
    // note: not all there state holder are valid depends on checkpoint behavior
    // in memory state holder will have a double buffer for transfer
    std::shared_ptr<Buffer> in_memory_holder_;
    std::shared_ptr<FileStream> on_disk_holder_;
    std::string filepath_;
    std::shared_ptr<Buffer> on_device_holder_;

    bitmask::bitmask<CheckPointBehavior> checkpoint_behavior_;
};

/**
 * @brief: local state is state owned by one process
 */
class LocalState : public BaseState {
public:
    LocalState() : replica_strategy_(0) {
    }
    LocalState(const std::vector<ReplicaStrategy>& replica_strategies)
        : LocalState() {
        for (auto&& replica_strategy : replica_strategies) {
            replica_strategy_ |= replica_strategy;
        }
    }
    void DoCheckpoint() {
        Tracker::Get()->SendStr("checkpoint");
        Tracker::Get()->SendInt(in_memory_holder()->size_in_bytes());
        Tracker::Get()->Send(in_memory_holder()->addr(),
                             in_memory_holder()->size_in_bytes());
        auto&& rank = Tracker::Get()->rank();
        auto&& neighbors = GetNeighbors(rank, num_replicas_);
        for (auto&& i = 0; i < num_replicas_; i++) {
            comm_->Send(in_memory_holder()->addr(),
                        in_memory_holder()->size_in_bytes(), neighbors[i]);
        }
    }
    void LoadCheckpoint() {
        Tracker::Get()->SendStr("loadcheckpoint");
        Tracker::Get()->Recv(in_memory_holder()->addr(),
                             in_memory_holder()->size_in_bytes());
        if (replica_strategy_ | ReplicaStrategy::WithPeers) {
            auto&& rank = Tracker::Get()->rank();
            auto&& neighbors = GetNeighbors(rank, num_replicas_);
            for (auto&& i = 0; i < num_replicas_; i++) {
                comm_->Recv(in_memory_holder()->addr(),
                            in_memory_holder()->size_in_bytes(), neighbors[i]);
            }
        }
    }

private:
    bitmask::bitmask<ReplicaStrategy> replica_strategy_;
    uint32_t num_replicas_;
    std::shared_ptr<ICommunicator> comm_;
    std::vector<std::shared_ptr<Buffer>> in_memory_peer_replicas_;
};

/**
 * @brief: global state is shared by all processes
 */
class GlobalState : public BaseState {
public:
    GlobalState();
};
class Checkpointer {
public:
    Checkpointer() = default;
    std::vector<GlobalState> states() const {
        return global_states_;
    }
    /**
     * @brief: move state the the government of checkpointer
     *
     * @tparam StateTy state type, either LocalState or Global State
     * @param state state to be appended
     */
    void AddGlobalState(const GlobalState& global_state) {
        global_states_.emplace_back(global_state);
    }

    void AddLocalState(const LocalState& local_state) {
        local_states_.emplace_back(local_state);
    }

private:
    /* @brief: all states need to be */
    std::vector<GlobalState> global_states_;
    std::vector<LocalState> local_states_;
    uint64_t seq_counter_;
    /* @brief: when enable double buffer, state transport will use another
     * buffer, after transport, all states will be updated togather*/
    bool enable_double_buffer_;
    /* @brief: number of replicas, this property is used only by local state*/
    int num_replicas_;
    /* @brief: checkpointer is associated to a unique communicator in order to
     * do checkpoint in background*/
    std::shared_ptr<comm::ICommunicator> comm_;
};
}  // namespace comm
}  // namespace rdc
