#pragma once

#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan.h"

#include <spdlog/spdlog.h>

#ifndef DEBUG
#define VK_CHECK(x)                                                                 \
    do {                                                                            \
        VkResult result = x;                                                        \
        if (result) {                                                               \
            spdlog::error("{}:{} {}", __FILE__, __LINE__, string_VkResult(result)); \
        }                                                                           \
    } while (0)
#else

inline void VK_CHECK(VkResult result)
{
    if (result) {
        spdlog::error("{}:{} {}", __FILE__, __LINE__, string_VkResult(result));
    }
}
#endif
