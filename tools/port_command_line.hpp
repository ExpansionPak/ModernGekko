#pragma once

// moderngekko-port's argument parsing, separated from the work it drives.
//
// It lived in main(), which meant the one part of the tool that every command
// goes through was the one part no test could reach: `run` forwards unknown
// arguments to the runner and `build` rejects them, `--` stops option parsing,
// and pgo-run adds three options that must not leak into the others. Adding a
// fourth command to that loop without a test is how `build --load-state` starts
// silently succeeding.

#include "pgo_support.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace moderngekko::port
{
namespace fs = std::filesystem;

struct BuildOptions
{
  std::string toolchain = "auto";
  // Empty means "leave the module template's own default alone". Any other
  // value is forwarded as RECOMPCORE_MODULE_OPT_LEVEL and folded into the
  // cache key, so an -O3 module cannot collide with an -O2 one.
  std::string opt_level;
  std::string backend;
  fs::path output;
  std::vector<std::string> runner_arguments;
  // PGO is a semantic build input, not an ambient flag the caller happens to
  // have exported: it changes what is compiled, what is linked, what DolRecomp
  // emits, and -- through PgoCacheIdentity -- which cache entry may answer.
  pgo::PgoBuildOptions pgo;
  // Only recorded, never part of the cache key. Kept beside the build so the
  // publish step can write the provenance block without pgo-run reaching back
  // into the artifact directory afterwards.
  pgo::ManifestFacts pgo_facts;
};

// Everything pgo-run needs that a plain build does not.
struct PgoRunOptions
{
  fs::path profile_dir;
  fs::path llvm_profdata;
  bool keep_work = false;
};

struct CommandLine
{
  std::string command;
  fs::path root;
  BuildOptions build;
  PgoRunOptions pgo_run;
  // Empty on success. `usage` distinguishes "you asked for something that does
  // not exist" from "that option is wrong", because the first wants the usage
  // text and the second wants to say what was wrong.
  std::string error;
  bool usage = false;

  explicit operator bool() const { return error.empty() && !usage; }
};

inline bool IsKnownCommand(std::string_view command)
{
  return command == "inspect" || command == "build" || command == "run" || command == "pgo-run";
}

inline CommandLine ParseCommandLine(int argc, const char* const* argv,
                                    std::string_view default_backend)
{
  CommandLine parsed;
  parsed.build.backend = std::string(default_backend);
  if (argc < 3)
  {
    parsed.usage = true;
    return parsed;
  }
  parsed.command = argv[1];
  parsed.root = fs::path(argv[2]);
  if (!IsKnownCommand(parsed.command))
  {
    parsed.usage = true;
    return parsed;
  }

  // `run` and `pgo-run` end in a runner invocation, so an argument this tool
  // does not recognise is meant for the runner rather than a mistake. `build`
  // and `inspect` have nowhere to forward one, so for them it is an error.
  const bool forwards_runner_arguments =
      parsed.command == "run" || parsed.command == "pgo-run";
  const bool is_pgo_run = parsed.command == "pgo-run";
  bool runner_arguments = false;
  for (int i = 3; i < argc; ++i)
  {
    const std::string argument = argv[i];
    if (runner_arguments)
      parsed.build.runner_arguments.push_back(argument);
    else if (argument == "--")
      runner_arguments = true;
    else if (argument == "--toolchain" && i + 1 < argc)
      parsed.build.toolchain = argv[++i];
    else if (argument == "--backend" && i + 1 < argc)
      parsed.build.backend = argv[++i];
    else if (argument == "--opt-level" && i + 1 < argc)
      parsed.build.opt_level = argv[++i];
    else if (argument == "--output" && i + 1 < argc)
      parsed.build.output = fs::path(argv[++i]);
    else if (argument == "--profile-dir" && i + 1 < argc && is_pgo_run)
      parsed.pgo_run.profile_dir = fs::path(argv[++i]);
    else if (argument == "--llvm-profdata" && i + 1 < argc && is_pgo_run)
      parsed.pgo_run.llvm_profdata = fs::path(argv[++i]);
    else if (argument == "--keep-work" && is_pgo_run)
      parsed.pgo_run.keep_work = true;
    else if (forwards_runner_arguments)
      parsed.build.runner_arguments.push_back(argument);
    else
    {
      parsed.error = "unknown or incomplete option: " + argument;
      return parsed;
    }
  }

  if (parsed.build.backend != "c" && parsed.build.backend != "llvm")
  {
    parsed.error = "unknown backend: " + parsed.build.backend;
    return parsed;
  }
  if (!parsed.build.opt_level.empty() &&
      (parsed.build.opt_level.size() != 1 || parsed.build.opt_level[0] < '0' ||
       parsed.build.opt_level[0] > '3'))
  {
    parsed.error = "opt level must be 0, 1, 2, or 3: " + parsed.build.opt_level;
    return parsed;
  }
  return parsed;
}
}  // namespace moderngekko::port
