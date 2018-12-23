#pragma once

#include "comm/communicator_base.h"
#include "io/file_io.h"
#include "io/memory_io.h"
#include "transport/buffer.h"

namespace rdc {
/*!
 \brief model struct
*/
enum class CheckPointBehavior : uint32_t {
    InMemory = 0b001,
    OnDisk = 0b010,
    InMemoryAndOnDisk = 0b011,
    OnDevice = 0b100,
    InMemoryAndOnDevice = 0b101,
};

enum class ReplicaStrategy : uint32_t {
    ReplicaWithTracker = 0b001,
    ReplicaWithPeers = 0b010,
    ReplicaWithTrackerAndPeers = 0b011,
    NoReplica = 0b100,
};

enum class StateKind : uint32_t {
    Local = 0b001,
    Global = 0b010,
    Unkown = 0b100,
};

/**
 * @brief: state base class, state is in a description of a piece of current
 */
class BaseState {
public:
    BaseState() = default;
    ~BaseState() = default;
    BaseState(const CheckPointBehavior& checkpoint_behavior)
        : checkpoint_behavior_(checkpoint_behavior) {
    }
    BaseState(const CheckPointBehavior& checkpoint_behavior,
              const std::string& filepath) {
    }
    BaseState(const CheckPointBehavior& checkpoint_behavior, void* ptr,
              const size_t& size) {
        CHECK(checkpoint_behavior != CheckPointBehavior::OnDisk);
        switch (checkpoint_behavior) {
        }
    }
    /*---------------------------peoperties-----------------------------*/
    uint64_t version_number() const {
    }

    std::string state_name() const {
        return state_name_;
    }

    uint64_t state_size() const {
        return state_size_;
    }

    MemoryFixedSizeStream in_memory_holder() const {
        return in_memory_holder_;
    }

    FileStream on_disk_holder() const {
        return on_disk_holder_;
    }

    MemoryFixedSizeStream on_device_holder() const {
        return on_device_holder_;
    }

private:
    uint64_t version_number_;
    std::string state_name_;
    size_t state_size_;
    bool enable_double_buffer_;
    // note: not all there state holder are valid depends on checkpoint behavior
    // in memory state holder will have a double buffer for transfer
    MemoryFixedSizeStream in_memory_holder_;
    MemoryFixedSizeStream in_memory_holder_for_transfer_;
    FileStream on_disk_holder_;
    MemoryFixedSizeStream on_device_holder_;
    MemoryFixedSizeStream on_device_holder_for_transfer_;
    CheckPointBehavior checkpoint_behavior_;
};

/**
 * @brief: local state is state owned by one process
 */
class LocalState : public BaseState {
public:
    LocalState();

private:
    ReplicaStrategy replica_strategy_;
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
    std::vector<GlobalState> states() const {
        return states_;
    }
    /**
     * @brief: move state the the government of checkpointer
     *
     * @tparam StateTy state type, either LocalState or Global State
     * @param state state to be appended
     */
    template <typename StateTy>
    void AddState(const StateTy& state) {
        states_.emplace_back(state);
    }

private:
    /* @brief: all states need to be */
    std::vector<GlobalState> states_;

    uint64_t seq_counter_;
    /* @brief: when enable double buffer, state transport will use another buffer, 
     * after transport, all states will be updated togather*/
    bool enable_double_buffer_;
    /* @brief: checkpointer is associated to a unique communicator in order to
     * do checkpoint in background*/
    std::unique_ptr<comm::ICommunicator> comm_;
};
}  // namespace rdc
