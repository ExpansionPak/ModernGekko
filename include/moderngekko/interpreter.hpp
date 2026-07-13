#pragma once

#include "moderngekko/cpu_state.h"

#include <cstdint>

namespace moderngekko
{
class AddressSpace;

enum class InterpreterStatus
{
  Executed = 0,
  UnsupportedInstruction,
  MemoryFault,
};

struct InterpreterResult
{
  InterpreterStatus status = InterpreterStatus::UnsupportedInstruction;
  std::uint32_t cycles = 0;
};

class Interpreter final
{
public:
  static InterpreterResult Step(CPUState& cpu, AddressSpace& memory);
};
}
