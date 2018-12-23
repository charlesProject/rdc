#pragma once
#include <memory>
namespace rdc {
namespace comm {
class Deamon {
public:
    Deamon();

    Deamon(const Deamon& deamon) = default;

    Deamon(Deamon&& deamon) = default;

    ~Deamon();

    /**
     * @brief:keep hearbeat with tracker
     */
    void Heartbeat();

private:
    // heartbeat related members
    /*!brief interval between two heartbeats*/
    uint64_t heartbeat_interval_;
    /*!brief background thread for keeping heartbeats*/
    std::unique_ptr<std::thread> heartbeat_thrd_;
    /*!brief timepoint of last valid heartbeat*/
    uint64_t last_heartbeat_timepoint_;
};
}  // namespace comm
}  // namespace rdc
