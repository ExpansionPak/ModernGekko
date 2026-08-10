// The layouts that matter here cannot be produced on demand by whatever machine
// runs the tests -- a part with 3D V-Cache on one die only, a part where every
// core shares a single cache -- so the buffer the OS would return is built by
// hand and the selection rule is checked against it.
#include "cache_affinity.hpp"

#include <cstring>
#include <vector>

namespace {
using moderngekko::frontend::CacheDomain;
using moderngekko::frontend::LargestSharedCache;

// One RelationCache record, laid out exactly as GetLogicalProcessorInformationEx
// returns it.
void AppendCache(std::vector<char> &buffer, BYTE level, DWORD size_bytes, KAFFINITY mask) {
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX entry{};
  entry.Relationship = RelationCache;
  entry.Size = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
  entry.Cache.Level = level;
  entry.Cache.CacheSize = size_bytes;
  entry.Cache.Type = CacheUnified;
  entry.Cache.GroupMask.Mask = mask;
  entry.Cache.GroupMask.Group = 0;
  const std::size_t at = buffer.size();
  buffer.resize(at + entry.Size);
  std::memcpy(buffer.data() + at, &entry, entry.Size);
}

// A record of some other relationship, which must be skipped rather than
// misread as a cache.
void AppendNonCache(std::vector<char> &buffer) {
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX entry{};
  entry.Relationship = RelationProcessorCore;
  entry.Size = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
  const std::size_t at = buffer.size();
  buffer.resize(at + entry.Size);
  std::memcpy(buffer.data() + at, &entry, entry.Size);
}

constexpr DWORD MB = 1024 * 1024;
} // namespace

int main() {
  // A 9950X3D-shaped part: two CCDs, 3D V-Cache on one. The rule must choose the
  // stacked die, which is the whole point of the change.
  {
    std::vector<char> buffer;
    AppendCache(buffer, 1, 32 * 1024, 0x0003);
    AppendCache(buffer, 2, 1 * MB, 0x00FF);
    AppendCache(buffer, 3, 96 * MB, 0xFFFF);         // V-Cache die
    AppendCache(buffer, 3, 32 * MB, 0xFFFF0000ULL);  // plain die
    const CacheDomain domain = LargestSharedCache(buffer.data(), buffer.size());
    if (!domain || domain.mask != 0xFFFF || domain.size != 96 * MB)
      return 1;
  }

  // Reported in the other order, the answer must not change.
  {
    std::vector<char> buffer;
    AppendCache(buffer, 3, 32 * MB, 0xFFFF0000ULL);
    AppendCache(buffer, 3, 96 * MB, 0xFFFF);
    const CacheDomain domain = LargestSharedCache(buffer.data(), buffer.size());
    if (!domain || domain.mask != 0xFFFF)
      return 2;
  }

  // A part where every core shares one L3: the mask covers all of them, so
  // pinning to it is a no-op rather than a restriction. This is what keeps the
  // change correct on hardware without a V-Cache die.
  {
    std::vector<char> buffer;
    AppendCache(buffer, 3, 32 * MB, 0xFFFFFFFFULL);
    const CacheDomain domain = LargestSharedCache(buffer.data(), buffer.size());
    if (!domain || domain.mask != 0xFFFFFFFFULL)
      return 3;
  }

  // Equal sizes keep the first match, so the result does not depend on the order
  // the OS happens to report caches in.
  {
    std::vector<char> buffer;
    AppendCache(buffer, 3, 32 * MB, 0x00FF);
    AppendCache(buffer, 3, 32 * MB, 0xFF00);
    const CacheDomain domain = LargestSharedCache(buffer.data(), buffer.size());
    if (domain.mask != 0x00FF)
      return 4;
  }

  // Only the requested level counts. A machine with no L3 at all yields nothing
  // to pin to, and the caller must leave affinity alone rather than pin to an L2.
  {
    std::vector<char> buffer;
    AppendCache(buffer, 1, 64 * 1024, 0x0003);
    AppendCache(buffer, 2, 8 * MB, 0x00FF);
    const CacheDomain domain = LargestSharedCache(buffer.data(), buffer.size());
    if (domain)
      return 5;
    // Asking for L2 explicitly still works.
    const CacheDomain l2 = LargestSharedCache(buffer.data(), buffer.size(), 2);
    if (!l2 || l2.mask != 0x00FF)
      return 6;
  }

  // Records of other relationships are skipped, not misread.
  {
    std::vector<char> buffer;
    AppendNonCache(buffer);
    AppendCache(buffer, 3, 96 * MB, 0x00FF);
    AppendNonCache(buffer);
    const CacheDomain domain = LargestSharedCache(buffer.data(), buffer.size());
    if (!domain || domain.mask != 0x00FF)
      return 7;
  }

  // An empty buffer is not an error; there is simply nothing to pin to.
  if (LargestSharedCache(nullptr, 0))
    return 8;

  // A record claiming Size 0 would never advance the cursor. The scan must stop
  // instead of spinning forever, and keep whatever it found before it.
  {
    std::vector<char> buffer;
    AppendCache(buffer, 3, 96 * MB, 0x00FF);
    auto *second = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(
        buffer.data() + buffer.size());
    buffer.resize(buffer.size() + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX));
    second = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(
        buffer.data() + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX));
    std::memset(second, 0, sizeof(*second));
    second->Relationship = RelationCache;
    second->Size = 0;
    const CacheDomain domain = LargestSharedCache(buffer.data(), buffer.size());
    if (!domain || domain.mask != 0x00FF)
      return 9;
  }

  // A buffer that ends mid-record must not be read past its end.
  {
    std::vector<char> buffer;
    AppendCache(buffer, 3, 96 * MB, 0x00FF);
    const CacheDomain domain = LargestSharedCache(buffer.data(), buffer.size() - 8);
    if (domain)
      return 10;
  }

  // --- the opt-in ----------------------------------------------------------
  {
    using moderngekko::frontend::AffinityEnabled;
    if (AffinityEnabled(nullptr) || AffinityEnabled("") || AffinityEnabled("0"))
      return 11;
    if (!AffinityEnabled("1") || AffinityEnabled("true") || AffinityEnabled("10"))
      return 12;
  }

  // --- the Win32 calls themselves ------------------------------------------
  // Applied to this process and read back out of the OS, so the test observes
  // what actually happened rather than merely trusting the return value.
  {
    using moderngekko::frontend::ApplyCacheDomain;
    const HANDLE self = GetCurrentProcess();
    DWORD_PTR original_affinity = 0;
    DWORD_PTR system_affinity = 0;
    if (!GetProcessAffinityMask(self, &original_affinity, &system_affinity))
      return 13;
    // The lowest core this process is already allowed on: a mask has to be a
    // subset of the process's current affinity or the call fails.
    const KAFFINITY subset = original_affinity & (~original_affinity + 1);
    const CacheDomain domain{subset, 96 * MB};

    const auto result = ApplyCacheDomain(self, domain);
    DWORD_PTR applied = 0;
    DWORD_PTR ignored = 0;
    GetProcessAffinityMask(self, &applied, &ignored);
    // Restore before reporting, so a failure does not leave the rest of the
    // suite pinned to one core.
    SetProcessAffinityMask(self, original_affinity);

    if (!result.affinity_set || applied != subset)
      return 14;

    DWORD_PTR restored = 0;
    GetProcessAffinityMask(self, &restored, &ignored);
    if (restored != original_affinity)
      return 15;

    // An empty domain must leave the process alone rather than pin it to
    // nothing: there was simply no L3 to find.
    const auto none = ApplyCacheDomain(self, CacheDomain{});
    GetProcessAffinityMask(self, &applied, &ignored);
    if (none.affinity_set || applied != original_affinity)
      return 16;
  }

  return 0;
}
