#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Events.hpp"

struct TimeObject {
    std::chrono::time_point<std::chrono::steady_clock> start;
    std::chrono::time_point<std::chrono::steady_clock> end;
    std::chrono::milliseconds duration;
};

class Timer : public EventReceiver {
  public:
    static void startTimer(const std::string& timerName);
    static void stopTimer(const std::string& timerName);

    static void ImGuiRender();

  private:
    static std::mutex s_TimeLock;
    static std::unordered_map<std::string, TimeObject> s_TrackedTimes;
};
