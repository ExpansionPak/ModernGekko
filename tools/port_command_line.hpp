#pragma once

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
  std::string opt_level;
  std::string backend;
  bool state_in_memory = false;
  fs::path output;
  std::vector<std::string> runner_arguments;
  pgo::PgoBuildOptions pgo;
  pgo::ManifestFacts pgo_facts;
  bool make_active = true;
};

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
    else if (argument == "--state-in-memory")
      parsed.build.state_in_memory = true;
    else if (argument == "--no-state-in-memory")
      parsed.build.state_in_memory = false;
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
