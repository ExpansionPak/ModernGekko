#include "moderngekko/cpu_state.h"
#include "moderngekko/module_loader.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv)
{
  if (argc < 2 || argc > 3)
  {
    std::cerr << "usage: moderngekko-module-info <module> [game-id]\n";
    return 2;
  }

  const ModernGekkoModuleRequirements requirements = {
      MODERNGEKKO_CPU_ABI_VERSION,
      static_cast<std::uint32_t>(sizeof(CPUState)),
      argc == 3 ? argv[2] : nullptr,
  };

  moderngekko::ModuleLibrary module;
  const moderngekko::ModuleLoadResult result = module.Open(argv[1], requirements);
  if (result.status != moderngekko::ModuleLoadStatus::Ok)
  {
    std::cerr << "module load failed: " << static_cast<int>(result.status);
    if (result.status == moderngekko::ModuleLoadStatus::DescriptorRejected)
      std::cerr << " (" << moderngekko_module_status_string(result.validation_status) << ")";
    std::cerr << '\n';
    return 1;
  }

  const ModernGekkoModuleDesc& descriptor = *module.GetDescriptor();
  std::cout << "game_id=" << descriptor.game_id << '\n';
  std::cout << "module_abi=" << descriptor.abi_version << '\n';
  std::cout << "cpu_abi=" << descriptor.cpu_abi_version << '\n';
  std::cout << "cpu_state_size=" << descriptor.cpu_state_size << '\n';
  std::cout << "entry_point=0x" << std::hex << std::setw(8) << std::setfill('0')
            << descriptor.entry_point << std::dec << '\n';
  std::cout << "code_ranges=" << descriptor.num_code_ranges << '\n';
  std::cout << "smc_ranges=" << descriptor.num_smc_ranges << '\n';
  std::cout << "chunk_ranges=" << descriptor.num_chunk_ranges << '\n';
  return 0;
}
