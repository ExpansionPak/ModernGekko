// pgo-run added a fourth command and three options to a parser that every
// existing command already goes through. These cases are mostly about what did
// NOT change: `build` still rejects a stray argument, `run` still forwards one,
// `--` still stops option parsing, and the pgo-run options do not exist for
// anything else.
#include "port_command_line.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace port = moderngekko::port;

namespace
{
int g_failures = 0;

void Check(bool condition, const std::string& what)
{
  if (condition)
    return;
  std::cerr << "FAIL: " << what << '\n';
  ++g_failures;
}

port::CommandLine Parse(std::vector<const char*> arguments,
                        std::string_view default_backend = "c")
{
  return port::ParseCommandLine(static_cast<int>(arguments.size()), arguments.data(),
                                default_backend);
}

void TestExistingCommands()
{
  const port::CommandLine inspect = Parse({"moderngekko-port", "inspect", "extracted/GM4E01"});
  Check(static_cast<bool>(inspect), "inspect failed to parse: " + inspect.error);
  Check(inspect.command == "inspect", "inspect command misparsed");
  Check(inspect.root == std::filesystem::path("extracted/GM4E01"), "inspect root misparsed");
  Check(inspect.build.backend == "c", "the default backend was not applied");

  const port::CommandLine build =
      Parse({"moderngekko-port", "build", "extracted/GM4E01", "--backend", "llvm", "--toolchain",
             "clang", "--opt-level", "3", "--output", "/modules"});
  Check(static_cast<bool>(build), "build failed to parse: " + build.error);
  Check(build.build.backend == "llvm", "--backend misparsed");
  Check(build.build.toolchain == "clang", "--toolchain misparsed");
  Check(build.build.opt_level == "3", "--opt-level misparsed");
  Check(build.build.output == std::filesystem::path("/modules"), "--output misparsed");

  // build has nowhere to forward an argument it does not know, so it must say
  // so rather than accept it silently.
  const port::CommandLine stray = Parse({"moderngekko-port", "build", "game", "--load-state"});
  Check(!stray, "build accepted an option it does not have");
  Check(stray.error.find("--load-state") != std::string::npos,
        "the error does not name the offending option: " + stray.error);

  const port::CommandLine incomplete = Parse({"moderngekko-port", "build", "game", "--backend"});
  Check(!incomplete, "build accepted --backend with no value");

  // run forwards what it does not recognise, with or without the separator.
  const port::CommandLine run =
      Parse({"moderngekko-port", "run", "game", "--graphics", "Vulkan"});
  Check(static_cast<bool>(run), "run failed to parse: " + run.error);
  Check(run.build.runner_arguments == std::vector<std::string>{"--graphics", "Vulkan"},
        "run did not forward its unknown arguments");

  const port::CommandLine separated = Parse({"moderngekko-port", "run", "game", "--backend",
                                             "llvm", "--", "--load-state", "states/race.sav"});
  Check(separated.build.backend == "llvm", "--backend before -- was not applied");
  Check(separated.build.runner_arguments ==
            std::vector<std::string>{"--load-state", "states/race.sav"},
        "arguments after -- were not forwarded verbatim");
}

void TestSeparatorStopsOptionParsing()
{
  // A runner option that shares a name with one of this tool's own must reach
  // the runner unchanged once -- has been seen.
  const port::CommandLine parsed = Parse(
      {"moderngekko-port", "pgo-run", "game", "--", "--output", "runner-owned", "--keep-work"});
  Check(static_cast<bool>(parsed), "pgo-run failed to parse: " + parsed.error);
  Check(parsed.build.output.empty(), "--output after -- was consumed by the port");
  Check(!parsed.pgo_run.keep_work, "--keep-work after -- was consumed by the port");
  Check(parsed.build.runner_arguments ==
            std::vector<std::string>{"--output", "runner-owned", "--keep-work"},
        "arguments after -- were not forwarded verbatim");
}

void TestPgoRun()
{
  const port::CommandLine parsed =
      Parse({"moderngekko-port", "pgo-run", "extracted/GM4E01", "--backend", "llvm",
             "--profile-dir", "build/pgo/GM4E01", "--llvm-profdata", "/opt/llvm/llvm-profdata",
             "--keep-work", "--", "--load-state", "states/race.sav", "--no-mods"});
  Check(static_cast<bool>(parsed), "pgo-run failed to parse: " + parsed.error);
  Check(parsed.command == "pgo-run", "pgo-run command misparsed");
  Check(parsed.build.backend == "llvm", "pgo-run --backend misparsed");
  Check(parsed.pgo_run.profile_dir == std::filesystem::path("build/pgo/GM4E01"),
        "--profile-dir misparsed");
  Check(parsed.pgo_run.llvm_profdata == std::filesystem::path("/opt/llvm/llvm-profdata"),
        "--llvm-profdata misparsed");
  Check(parsed.pgo_run.keep_work, "--keep-work misparsed");
  Check(parsed.build.runner_arguments ==
            std::vector<std::string>{"--load-state", "states/race.sav", "--no-mods"},
        "pgo-run did not forward the runner arguments verbatim");
  // pgo-run starts with PGO off; the workflow sets generate and use itself, and
  // a mode arriving from the command line would bypass that sequencing.
  Check(parsed.build.pgo.mode == moderngekko::pgo::PgoMode::Off,
        "parsing set a PGO mode of its own");

  // The pgo-run options belong to pgo-run. On build they have nowhere to go,
  // and on run they must go to the runner rather than be silently swallowed.
  const port::CommandLine on_build =
      Parse({"moderngekko-port", "build", "game", "--keep-work"});
  Check(!on_build, "build accepted --keep-work");

  const port::CommandLine on_run = Parse({"moderngekko-port", "run", "game", "--keep-work"});
  Check(static_cast<bool>(on_run), "run failed to parse --keep-work: " + on_run.error);
  Check(on_run.build.runner_arguments == std::vector<std::string>{"--keep-work"},
        "run swallowed --keep-work instead of forwarding it");
}

void TestValidation()
{
  Check(!Parse({"moderngekko-port", "build", "game", "--backend", "cpp"}),
        "an unknown backend was accepted");
  Check(!Parse({"moderngekko-port", "build", "game", "--opt-level", "4"}),
        "an out-of-range opt level was accepted");
  Check(!Parse({"moderngekko-port", "build", "game", "--opt-level", "fast"}),
        "a non-numeric opt level was accepted");
  Check(static_cast<bool>(Parse({"moderngekko-port", "build", "game", "--opt-level", "0"})),
        "-O0 was rejected");

  const port::CommandLine unknown = Parse({"moderngekko-port", "frobnicate", "game"});
  Check(unknown.usage, "an unknown command did not ask for the usage text");
  const port::CommandLine bare = Parse({"moderngekko-port"});
  Check(bare.usage, "a bare invocation did not ask for the usage text");
  const port::CommandLine no_root = Parse({"moderngekko-port", "inspect"});
  Check(no_root.usage, "a missing game root did not ask for the usage text");

  const port::CommandLine llvm_default =
      Parse({"moderngekko-port", "build", "game"}, "llvm");
  Check(llvm_default.build.backend == "llvm", "the build's default backend was not honoured");
}
}  // namespace

int main()
{
  TestExistingCommands();
  TestSeparatorStopsOptionParsing();
  TestPgoRun();
  TestValidation();
  return g_failures == 0 ? 0 : 1;
}
