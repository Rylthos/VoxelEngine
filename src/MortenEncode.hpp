#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Morten {
uint64_t splitBy3(uint32_t x);
uint64_t encode(glm::uvec3 position);

uint32_t compactBy3(uint64_t x);
glm::uvec3 decode(uint64_t x);
} // namespace Morten
