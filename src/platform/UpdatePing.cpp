#include "platform/UpdatePing.hpp"

#include "core/UpdateCheck.hpp"
#include "core/Version.hpp"
#include "platform/UpdatePingInternal.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace galaxy {
namespace {

std::mutex waitMutex;
std::condition_variable waitCv;
std::atomic<bool> stopRequested{false};
std::thread worker;

}  // namespace

void startUpdateCheck() {
    if (worker.joinable()) {
        return;
    }
    stopRequested.store(false);
    const std::string version = versionString();
    const std::string platform = hostPlatformName();
    const std::string operatingSystem = hostOperatingSystem();
    worker = std::thread([version, platform, operatingSystem] {
        const auto send = [&](bool later) {
            postUpdateCheck(updateCheckBody(version, platform, operatingSystem, later));
        };
        send(false);
        std::unique_lock<std::mutex> lock(waitMutex);
        while (!stopRequested.load()) {
            if (waitCv.wait_for(lock, std::chrono::minutes(30), [] { return stopRequested.load(); })) {
                break;
            }
            lock.unlock();
            send(true);
            lock.lock();
        }
    });
}

void stopUpdateCheck() {
    {
        std::lock_guard<std::mutex> lock(waitMutex);
        stopRequested.store(true);
    }
    waitCv.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

}  // namespace galaxy
