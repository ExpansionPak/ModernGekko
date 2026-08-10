#pragma once

// Which cores share the largest cache at a given level.
//
// Split out from the pinning itself so the rule can be tested against a
// synthetic topology: the interesting layouts -- a part with 3D V-Cache on one
// die only, a part where every core shares one cache -- cannot be produced on
// demand by the machine running the tests, and the rule is the part that decides
// whether the pin helps or does nothing.

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace moderngekko::frontend
{
struct CacheDomain
{
  KAFFINITY mask = 0;
  DWORD size = 0;

  // A zero mask means nothing matched, so there is nothing to pin to.
  explicit operator bool() const { return mask != 0; }
};

// Scans a GetLogicalProcessorInformationEx(RelationCache, ...) buffer.
//
// Ties keep the first match rather than the last, so the answer does not depend
// on the order the OS happens to report caches in.
//
// Multi-group machines would need every group considered; a single group covers
// up to 64 logical processors, which is all this targets.
inline CacheDomain LargestSharedCache(const void* records, std::size_t bytes, BYTE level = 3)
{
  CacheDomain best;
  const auto* base = static_cast<const char*>(records);
  for (std::size_t offset = 0;
       offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= bytes;)
  {
    const auto* entry =
        reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(base + offset);
    // A zero Size would not advance, so this stops rather than spinning on a
    // truncated or malformed buffer.
    if (entry->Size == 0)
      break;
    if (entry->Relationship == RelationCache && entry->Cache.Level == level &&
        entry->Cache.CacheSize > best.size)
    {
      best.size = entry->Cache.CacheSize;
      best.mask = entry->Cache.GroupMask.Mask;
    }
    offset += entry->Size;
  }
  return best;
}

// Process-wide affinity can interfere with Dolphin's worker threads, so require
// an exact, explicit opt-in instead of changing every launch by default.
inline bool AffinityEnabled(const char* value)
{
  return value != nullptr && value[0] == '1' && value[1] == '\0';
}

struct PinResult
{
  bool affinity_set = false;
};

// Applies a domain to a process. Separate from choosing one so a test can hand
// it a real process handle and read the result back out of the OS.
//
// An empty domain is not an error: there was nothing to pin to, and the
// process's existing affinity is left alone.
inline PinResult ApplyCacheDomain(HANDLE process, const CacheDomain& domain)
{
  PinResult result;
  if (!domain)
    return result;
  result.affinity_set = SetProcessAffinityMask(process, domain.mask) != FALSE;
  return result;
}
}  // namespace moderngekko::frontend
#endif  // _WIN32
