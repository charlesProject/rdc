#include <chrono>
#include <thread>
#include "comm/deamon.h"
#include "comm/tracker.h"
#include "common/env.h"
#include "core/logging.h"
namespace rdc {
namespace comm {
Deamon::Deamon() {
    heartbeat_interval_ = Env::Get()->GetEnv("RDC_HEARTBEAT_INTERVAL", 1000);
    // spin util connected to tracker
    heartbeat_thrd_.reset(new std::thread(&Deamon::Heartbeat, this));
}

Deamon::~Deamon() {
    heartbeat_thrd_->join();
}

void Deamon::Heartbeat() {
    while (!Tracker::Get()->tracker_connected()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(heartbeat_interval_));
    }
    while (Tracker::Get()->tracker_connected()) {
        Tracker::Get()->Lock();
        if (!Tracker::Get()->tracker_connected()) {
            Tracker::Get()->UnLock();
            break;
        }
        Tracker::Get()->SendStr("heartbeat");
        std::string heartbeat_token;
        Tracker::Get()->RecvStr(heartbeat_token);
        CHECK_EQ(heartbeat_token, "heartbeat_done");
        last_heartbeat_timepoint_ =
            std::chrono::steady_clock::now().time_since_epoch().count();
        Tracker::Get()->UnLock();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(heartbeat_interval_));
    }
}
}  // namespace comm
}  // namespace rdc
