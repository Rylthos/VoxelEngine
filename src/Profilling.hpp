#pragma once

#define PROF_TRACY

#ifdef PROF_TRACY

#include <vulkan/vulkan.h>

#include "tracy/Tracy.hpp"
#include "tracy/TracyVulkan.hpp"

extern TracyVkCtx g_TracyVkCtx;

#define PROF_VK_ZONE(cmd, name) TracyVkZone(g_TracyVkCtx, cmd, name)
#define PROF_VK_COLLECT(cmd) TracyVkCollect(g_TracyVkCtx, cmd)

#define PROF_LOCKABLE_MUTEX(type, var, name) TracyLockableN(type, var, name)
#define PROF_LOCKABLE(type) tracy::lockable<type>
#define PROF_LOCKABLE_BASE(type) LockableBase(type)

#define PROF_THREAD_NAME(name) tracy::SetThreadName(name);

#define PROF_ZONE_SCOPED ZoneScoped
#define PROF_ZONE_1 Zone1
#define PROF_ZONE_2 Zone2
#define PROF_ZONE_3 Zone3
#define PROF_ZONE_4 Zone4
#define PROF_ZONE_5 Zone5
#define PROF_ZONE_NAMED_N(zone, name, enabled) ZoneNamedN(zone, name, enabled)

#define PROF_FRAME_MARK FrameMark

template<class T>
using PROF_lockable_T = tracy::Lockable<T>;

#endif

#ifndef PROF_TRACY

#define PROF_VK_ZONE(cmd, name)
#define PROF_VK_COLLECT(cmd)

#define PROF_LOCKABLE_MUTEX(type, var, name) type var
#define PROF_LOCKABLE(type) type
#define PROF_LOCKABLE_BASE(type) type

#define PROF_THREAD_NAME(name)

#define PROF_ZONE_SCOPED
#define PROF_ZONE_1
#define PROF_ZONE_2
#define PROF_ZONE_3
#define PROF_ZONE_4
#define PROF_ZONE_5
#define PROF_ZONE_NAMED_N(zone, name, enabled)

#define PROF_FRAME_MARK

template<class T>
using PROF_lockable_T = T;

#endif
