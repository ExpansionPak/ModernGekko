// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "moderngekko/interpreter.hpp"

#include "Core/PowerPC/ConditionRegister.h"
#include "Core/PowerPC/Gekko.h"
#include "moderngekko/address_space.hpp"

#include <bit>
#include <cstdint>
#include <limits>

namespace moderngekko
{
namespace
{
PowerPC::ConditionRegister LoadConditionRegister(const CPUState& cpu)
{
  PowerPC::ConditionRegister condition_register{};
  condition_register.Set(cpu.cr);
  return condition_register;
}

void StoreConditionRegister(CPUState& cpu, const PowerPC::ConditionRegister& condition_register)
{
  cpu.cr = condition_register.Get();
}

bool GetSummaryOverflow(const CPUState& cpu)
{
  return UReg_XER{cpu.xer}.SO;
}

void SetCarry(CPUState& cpu, bool carry)
{
  UReg_XER xer{cpu.xer};
  xer.CA = carry;
  cpu.xer = xer.Hex;
}

bool Carry(std::uint32_t first, std::uint32_t second)
{
  return second > ~first;
}

void UpdateCr0(CPUState& cpu, std::uint32_t value)
{
  std::uint32_t field = value == 0u ? PowerPC::CR_EQ :
                        static_cast<std::int32_t>(value) < 0 ? PowerPC::CR_LT : PowerPC::CR_GT;
  if (GetSummaryOverflow(cpu))
    field |= PowerPC::CR_SO;

  auto condition_register = LoadConditionRegister(cpu);
  condition_register.SetField(0u, field);
  StoreConditionRegister(cpu, condition_register);
}

template <typename T>
void Compare(CPUState& cpu, std::uint32_t field_index, T first, T second)
{
  std::uint32_t field = first < second ? PowerPC::CR_LT :
                        first > second ? PowerPC::CR_GT : PowerPC::CR_EQ;
  if (GetSummaryOverflow(cpu))
    field |= PowerPC::CR_SO;

  auto condition_register = LoadConditionRegister(cpu);
  condition_register.SetField(field_index, field);
  StoreConditionRegister(cpu, condition_register);
}

bool ExecuteOpcode31(CPUState& cpu, UGeckoInstruction instruction)
{
  const std::uint32_t a = cpu.gpr[instruction.RA];
  const std::uint32_t b = cpu.gpr[instruction.RB];
  const std::uint32_t source = cpu.gpr[instruction.RS];
  std::uint32_t destination = instruction.RA;
  std::uint32_t result = 0u;

  switch (instruction.SUBOP10)
  {
  case 0u:
    Compare<std::int32_t>(cpu, instruction.CRFD, static_cast<std::int32_t>(a),
                          static_cast<std::int32_t>(b));
    return true;
  case 24u:
    result = b >= 32u ? 0u : source << (b & 31u);
    break;
  case 26u:
    result = static_cast<std::uint32_t>(std::countl_zero(source));
    break;
  case 28u:
    result = source & b;
    break;
  case 32u:
    Compare<std::uint32_t>(cpu, instruction.CRFD, a, b);
    return true;
  case 40u:
    destination = instruction.RD;
    result = b - a;
    break;
  case 124u:
    result = ~(source | b);
    break;
  case 235u:
    destination = instruction.RD;
    result = static_cast<std::uint32_t>(static_cast<std::int64_t>(
        static_cast<std::int32_t>(source)) * static_cast<std::int32_t>(b));
    break;
  case 266u:
    destination = instruction.RD;
    result = a + b;
    break;
  case 316u:
    result = source ^ b;
    break;
  case 444u:
    result = source | b;
    break;
  case 536u:
    result = b >= 32u ? 0u : source >> (b & 31u);
    break;
  case 922u:
    result = static_cast<std::uint32_t>(static_cast<std::int32_t>(
        static_cast<std::int16_t>(source)));
    break;
  case 954u:
    result = static_cast<std::uint32_t>(static_cast<std::int32_t>(
        static_cast<std::int8_t>(source)));
    break;
  default:
    return false;
  }

  cpu.gpr[destination] = result;
  if (instruction.Rc)
    UpdateCr0(cpu, result);
  return true;
}

bool Execute(CPUState& cpu, UGeckoInstruction instruction)
{
  switch (instruction.OPCD)
  {
  case 7u:
    cpu.gpr[instruction.RD] = static_cast<std::uint32_t>(static_cast<std::int64_t>(
        static_cast<std::int32_t>(cpu.gpr[instruction.RA])) * instruction.SIMM_16);
    return true;
  case 8u:
  {
    const std::uint32_t a = cpu.gpr[instruction.RA];
    const std::uint32_t immediate = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(instruction.SIMM_16));
    cpu.gpr[instruction.RD] = immediate - a;
    SetCarry(cpu, a == 0u || Carry(0u - a, immediate));
    return true;
  }
  case 10u:
    Compare<std::uint32_t>(cpu, instruction.CRFD, cpu.gpr[instruction.RA], instruction.UIMM);
    return true;
  case 11u:
    Compare<std::int32_t>(cpu, instruction.CRFD,
                          static_cast<std::int32_t>(cpu.gpr[instruction.RA]),
                          instruction.SIMM_16);
    return true;
  case 12u:
  case 13u:
  {
    const std::uint32_t a = cpu.gpr[instruction.RA];
    const std::uint32_t immediate = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(instruction.SIMM_16));
    cpu.gpr[instruction.RD] = a + immediate;
    SetCarry(cpu, Carry(a, immediate));
    if (instruction.OPCD == 13u)
      UpdateCr0(cpu, cpu.gpr[instruction.RD]);
    return true;
  }
  case 14u:
    cpu.gpr[instruction.RD] = instruction.RA == 0u ?
        static_cast<std::uint32_t>(static_cast<std::int32_t>(instruction.SIMM_16)) :
        cpu.gpr[instruction.RA] + static_cast<std::uint32_t>(
            static_cast<std::int32_t>(instruction.SIMM_16));
    return true;
  case 15u:
  {
    const std::uint32_t immediate = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(instruction.SIMM_16) * 65536);
    cpu.gpr[instruction.RD] = instruction.RA == 0u ? immediate :
        cpu.gpr[instruction.RA] + immediate;
    return true;
  }
  case 16u:
  {
    if ((instruction.BO & 4u) == 0u)
      --cpu.ctr;
    auto condition_register = LoadConditionRegister(cpu);
    const bool counter_ok = (instruction.BO & 4u) != 0u ||
        ((cpu.ctr != 0u) ^ ((instruction.BO & 2u) != 0u));
    const bool condition_ok = (instruction.BO & 16u) != 0u ||
        (condition_register.GetBit(instruction.BI) == ((instruction.BO >> 3) & 1u));
    if (counter_ok && condition_ok)
    {
      if (instruction.LK)
        cpu.lr = cpu.pc + 4u;
      const std::uint32_t displacement = static_cast<std::uint32_t>(
          SignExt16(static_cast<std::int16_t>(instruction.BD << 2)));
      cpu.pc = instruction.AA ? displacement : cpu.pc + displacement;
    }
    else
    {
      cpu.pc += 4u;
    }
    return true;
  }
  case 18u:
  {
    if (instruction.LK)
      cpu.lr = cpu.pc + 4u;
    const std::uint32_t destination = static_cast<std::uint32_t>(SignExt26(instruction.LI << 2));
    cpu.pc = instruction.AA ? destination : cpu.pc + destination;
    return true;
  }
  case 20u:
  {
    const std::uint32_t mask = MakeRotationMask(instruction.MB, instruction.ME);
    cpu.gpr[instruction.RA] = (std::rotl(cpu.gpr[instruction.RS], instruction.SH) & mask) |
                              (cpu.gpr[instruction.RA] & ~mask);
    if (instruction.Rc)
      UpdateCr0(cpu, cpu.gpr[instruction.RA]);
    return true;
  }
  case 21u:
  {
    const std::uint32_t result = std::rotl(cpu.gpr[instruction.RS], instruction.SH) &
                                 MakeRotationMask(instruction.MB, instruction.ME);
    cpu.gpr[instruction.RA] = result;
    if (instruction.Rc)
      UpdateCr0(cpu, result);
    return true;
  }
  case 23u:
  {
    const std::uint32_t result = std::rotl(cpu.gpr[instruction.RS],
                                          static_cast<int>(cpu.gpr[instruction.RB] & 31u)) &
                                 MakeRotationMask(instruction.MB, instruction.ME);
    cpu.gpr[instruction.RA] = result;
    if (instruction.Rc)
      UpdateCr0(cpu, result);
    return true;
  }
  case 24u:
    cpu.gpr[instruction.RA] = cpu.gpr[instruction.RS] | instruction.UIMM;
    return true;
  case 25u:
    cpu.gpr[instruction.RA] = cpu.gpr[instruction.RS] | (instruction.UIMM << 16);
    return true;
  case 26u:
    cpu.gpr[instruction.RA] = cpu.gpr[instruction.RS] ^ instruction.UIMM;
    return true;
  case 27u:
    cpu.gpr[instruction.RA] = cpu.gpr[instruction.RS] ^ (instruction.UIMM << 16);
    return true;
  case 28u:
    cpu.gpr[instruction.RA] = cpu.gpr[instruction.RS] & instruction.UIMM;
    UpdateCr0(cpu, cpu.gpr[instruction.RA]);
    return true;
  case 29u:
    cpu.gpr[instruction.RA] = cpu.gpr[instruction.RS] & (instruction.UIMM << 16);
    UpdateCr0(cpu, cpu.gpr[instruction.RA]);
    return true;
  case 31u:
    return ExecuteOpcode31(cpu, instruction);
  default:
    return false;
  }
}
}

InterpreterResult Interpreter::Step(CPUState& cpu, AddressSpace& memory)
{
  std::uint32_t raw_instruction = 0u;
  if (!memory.Read32(cpu.pc, &raw_instruction))
    return {InterpreterStatus::MemoryFault, 0u};

  const std::uint32_t old_pc = cpu.pc;
  if (!Execute(cpu, UGeckoInstruction{raw_instruction}))
    return {InterpreterStatus::UnsupportedInstruction, 0u};
  if (cpu.pc == old_pc)
    cpu.pc += 4u;
  return {InterpreterStatus::Executed, 1u};
}
}
