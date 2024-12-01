#include "Profilling.hpp"

#ifdef PROF_TRACY
TracyVkCtx g_TracyVkCtx = 0;

#define PROF_VK_ZONE(cmd, name) TracyVkZone(g_TracyVkCtx, cmd, name)

#endif
