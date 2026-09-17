#pragma once

// Which cores share the largest cache at a given level.
//
// Split out from the pinning itself so the rule can be tested against a
// synthetic topology: the interesting layouts -- a part with 3D V-Cache on one
// die only, a part where every core shares one cache -- cannot be produced on
// demand by the machine running the tests, and the rule is the part that decides
// whether the pin helps or does nothing.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace moderngekko::frontend
{
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
}  // namespace moderngekko::frontend

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

#elif defined(__linux__)

#include <sched.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

// Linux exposes the same topology through sysfs. Each logical CPU has one
// directory per cache it sees; a level-3 entry lists every CPU sharing that
// cache in `shared_cpu_list`, so the distinct lists across all CPUs are the
// domains, and `size` picks between them.
//
// The sysfs root is a parameter so a test can point it at a fixture tree. The
// layouts that matter -- one die with a larger victim cache, one uniform cache
// spanning every core -- are not reproducible on demand on the test machine,
// which is the same reason the Windows rule takes a buffer rather than calling
// the OS itself.
namespace moderngekko::frontend
{
struct CacheDomain
{
  std::vector<int> cpus;
  unsigned long long size = 0;

  // No CPUs means nothing matched, so there is nothing to pin to.
  explicit operator bool() const { return !cpus.empty(); }
};

// "0-5,12-17" -> 0 1 2 3 4 5 12 13 14 15 16 17
//
// Malformed input yields an empty list rather than a partial one: a domain that
// is wrong is worse than a domain that is missing, because the caller pins to it.
inline std::vector<int> ParseCpuList(std::string_view text)
{
  std::vector<int> cpus;
  while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
    text.remove_suffix(1);
  while (!text.empty())
  {
    const std::size_t comma = text.find(',');
    std::string_view item = text.substr(0, comma);
    text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
    if (item.empty())
      return {};

    const std::size_t dash = item.find('-');
    const auto number = [](std::string_view s, int* out) {
      if (s.empty())
        return false;
      int value = 0;
      for (const char c : s)
      {
        if (c < '0' || c > '9')
          return false;
        // A CPU index this large is not a topology, it is a malformed file.
        if (value > 100000)
          return false;
        value = value * 10 + (c - '0');
      }
      *out = value;
      return true;
    };

    int first = 0;
    int last = 0;
    if (dash == std::string_view::npos)
    {
      if (!number(item, &first))
        return {};
      last = first;
    }
    else if (!number(item.substr(0, dash), &first) ||
             !number(item.substr(dash + 1), &last) || last < first)
    {
      return {};
    }
    for (int cpu = first; cpu <= last; cpu++)
      cpus.push_back(cpu);
  }
  return cpus;
}

// "32768K" / "32M" / "512" -> bytes. Returns 0 for anything unrecognised.
inline unsigned long long ParseCacheSize(std::string_view text)
{
  unsigned long long value = 0;
  std::size_t i = 0;
  for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; i++)
    value = value * 10 + static_cast<unsigned>(text[i] - '0');
  if (i == 0)
    return 0;
  if (i < text.size())
  {
    switch (text[i])
    {
    case 'K':
    case 'k':
      value *= 1024ull;
      break;
    case 'M':
    case 'm':
      value *= 1024ull * 1024ull;
      break;
    case 'G':
    case 'g':
      value *= 1024ull * 1024ull * 1024ull;
      break;
    case '\n':
    case '\0':
      break;
    default:
      return 0;
    }
  }
  return value;
}

inline std::string ReadSysfsFile(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input)
    return {};
  std::string text;
  std::getline(input, text);
  return text;
}

// Ties keep the first domain seen in CPU order, so the answer does not depend on
// directory iteration order. On a part whose dies are identical that choice is
// arbitrary and it does not matter which one wins -- what matters is that the
// process stops migrating between them.
inline CacheDomain LargestSharedCache(
    const std::filesystem::path& sysfs_root = "/sys/devices/system/cpu", int level = 3)
{
  CacheDomain best;
  std::error_code ec;
  std::vector<std::vector<int>> seen;
  for (int cpu = 0;; cpu++)
  {
    const std::filesystem::path cache_dir =
        sysfs_root / ("cpu" + std::to_string(cpu)) / "cache";
    if (!std::filesystem::exists(cache_dir, ec))
      break;
    for (int index = 0; index < 16; index++)
    {
      const std::filesystem::path entry = cache_dir / ("index" + std::to_string(index));
      if (!std::filesystem::exists(entry, ec))
        continue;
      if (ReadSysfsFile(entry / "level") != std::to_string(level))
        continue;
      std::vector<int> cpus = ParseCpuList(ReadSysfsFile(entry / "shared_cpu_list"));
      if (cpus.empty())
        continue;
      // The same domain is reported once per CPU in it; count it once.
      if (std::find(seen.begin(), seen.end(), cpus) != seen.end())
        continue;
      seen.push_back(cpus);
      const unsigned long long size = ParseCacheSize(ReadSysfsFile(entry / "size"));
      if (size > best.size)
        best = CacheDomain{std::move(cpus), size};
    }
  }
  return best;
}

// An empty domain is not an error: there was nothing to pin to, and the
// process's existing affinity is left alone.
//
// This pins the calling thread and every thread it later creates, which on Linux
// means passing 0 rather than a pid: sched_setaffinity on the process id moves
// only the main thread on some kernels, and Dolphin's workers are spawned after
// this runs.
inline PinResult ApplyCacheDomain(pid_t pid, const CacheDomain& domain)
{
  PinResult result;
  if (!domain)
    return result;
  cpu_set_t set;
  CPU_ZERO(&set);
  for (const int cpu : domain.cpus)
  {
    if (cpu < 0 || cpu >= CPU_SETSIZE)
      return result;
    CPU_SET(cpu, &set);
  }
  result.affinity_set = sched_setaffinity(pid, sizeof(set), &set) == 0;
  return result;
}
}  // namespace moderngekko::frontend
#endif  // _WIN32 / __linux__
