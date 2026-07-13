#include "moderngekko/address_space.hpp"
#include "moderngekko/interpreter.hpp"

#include <cstdint>

namespace
{
constexpr std::uint32_t EncodeX(std::uint32_t destination_or_source, std::uint32_t a,
                                std::uint32_t b, std::uint32_t subopcode)
{
  return (31u << 26) | (destination_or_source << 21) | (a << 16) | (b << 11) |
         (subopcode << 1);
}
}

int main()
{
  moderngekko::AddressSpace memory;
  CPUState cpu{};
  cpu.pc = 0x80001000u;

  memory.Write32(0x80001000u, 0x38600005u);
  memory.Write32(0x80001004u, 0x60640010u);
  memory.Write32(0x80001008u, 0x2C030005u);
  memory.Write32(0x8000100Cu, 0x41820008u);
  memory.Write32(0x80001010u, 0x38A00001u);
  memory.Write32(0x80001014u, 0x38A00002u);

  if (moderngekko::Interpreter::Step(cpu, memory).status !=
          moderngekko::InterpreterStatus::Executed ||
      cpu.gpr[3] != 5u)
  {
    return 1;
  }
  if (moderngekko::Interpreter::Step(cpu, memory).status !=
          moderngekko::InterpreterStatus::Executed ||
      cpu.gpr[4] != 0x15u)
  {
    return 2;
  }
  if (moderngekko::Interpreter::Step(cpu, memory).status !=
          moderngekko::InterpreterStatus::Executed)
  {
    return 3;
  }
  if (moderngekko::Interpreter::Step(cpu, memory).status !=
          moderngekko::InterpreterStatus::Executed ||
      cpu.pc != 0x80001014u)
  {
    return 4;
  }
  if (moderngekko::Interpreter::Step(cpu, memory).status !=
          moderngekko::InterpreterStatus::Executed ||
      cpu.gpr[5] != 2u)
  {
    return 5;
  }

  memory.Write32(cpu.pc, EncodeX(6u, 3u, 4u, 266u));
  if (moderngekko::Interpreter::Step(cpu, memory).status !=
          moderngekko::InterpreterStatus::Executed ||
      cpu.gpr[6] != 0x1Au || cpu.gpr[3] != 5u)
  {
    return 6;
  }

  memory.Write32(cpu.pc, EncodeX(3u, 7u, 4u, 444u));
  if (moderngekko::Interpreter::Step(cpu, memory).status !=
          moderngekko::InterpreterStatus::Executed ||
      cpu.gpr[7] != 0x15u || cpu.gpr[3] != 5u)
  {
    return 7;
  }

  memory.Write32(cpu.pc, 0x00000000u);
  if (moderngekko::Interpreter::Step(cpu, memory).status !=
      moderngekko::InterpreterStatus::UnsupportedInstruction)
  {
    return 8;
  }

  cpu.pc = 0x70000000u;
  if (moderngekko::Interpreter::Step(cpu, memory).status !=
      moderngekko::InterpreterStatus::MemoryFault)
  {
    return 9;
  }

  return 0;
}
