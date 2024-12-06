#pragma once

#include <mutex>

#include <vulkan/vulkan.h>

#include "Profilling.hpp"

struct Queue {
    VkQueue queue;
    uint32_t queueFamily;
    PROF_LOCKABLE_MUTEX(std::mutex, queueMutex, "Queue Lock");
};
