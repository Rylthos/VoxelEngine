#include "Timer.hpp"

#include <format>
#include <string>

#include "imgui.h"

std::mutex Timer::s_TimeLock;
std::unordered_map<std::string, TimeObject> Timer::s_TrackedTimes;

void Timer::startTimer(const std::string& name)
{
    TimeObject time;
    if (s_TrackedTimes.contains(name))
    {
        time = s_TrackedTimes.at(name);
    }
    time.start = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lk(s_TimeLock);

    s_TrackedTimes.insert_or_assign(name, time);
}

void Timer::stopTimer(const std::string& name)
{
    std::chrono::time_point endTime = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lk(s_TimeLock);

    TimeObject t = s_TrackedTimes.at(name);

    t.end = endTime;

    t.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::duration(t.end - t.start));

    s_TrackedTimes.insert_or_assign(name, t);
    // s_TrackedTimes.at(name) = t;
}

void Timer::ImGuiRender()
{
    std::map<int64_t, std::string> times;
    for (auto& pair : s_TrackedTimes)
    {
        times.emplace(pair.second.duration.count(), pair.first);
    }

    if (ImGui::Begin("Timings"))
    {
        for (auto itr = times.rbegin(); itr != times.rend(); itr++)
        {
            ImGui::Text("%s", std::format("{}: {}ms", itr->second, itr->first).c_str());
        }
    }
    ImGui::End();
}
