#include "moderngekko/dolphin_shader_compiler.hpp"

#include <array>
#include <string_view>

int main()
{
  moderngekko::DolphinShaderCompiler::SetCacheDirectory("test-cache");
  std::array<std::uint32_t, 256> cp{};
  std::array<std::uint32_t, 0x1058> xf{};
  std::array<std::uint32_t, 256> bp{};
  cp[0x50u] = 1u << 13u;
  bp[0xC0u] = 15u | (15u << 4) | (15u << 8) | (10u << 12) | (1u << 19);
  bp[0xC1u] = (7u << 4) | (7u << 7) | (7u << 10) | (5u << 13) | (1u << 19);
  const moderngekko::DolphinShaderBundle shaders = moderngekko::DolphinShaderCompiler::Compile(
      {cp, xf, bp}, moderngekko::GxTopology::Triangles, 0,
      moderngekko::DolphinShaderApi::OpenGl);
  if (shaders.pixel.empty() ||
      shaders.pixel.find("void main") == std::string_view::npos)
    return 1;
  if (shaders.pixel.find("prev") == std::string_view::npos)
    return 2;
  if (shaders.vertex.find("void main") == std::string::npos)
    return 3;
  if (shaders.uber_pixel.find("void main") == std::string::npos)
    return 4;
  if (shaders.uber_vertex.find("void main") == std::string::npos)
    return 5;
  if (shaders.geometry.find("void main") == std::string::npos)
    return 6;
  if (shaders.vertex_uid == 0 || shaders.pixel_uid == 0 || shaders.geometry_uid == 0 ||
      shaders.uber_vertex_uid == 0 || shaders.uber_pixel_uid == 0)
    return 7;
  return 0;
}
