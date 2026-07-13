#pragma once

#include "moderngekko/gx_state_backend.hpp"

#include <cstdint>
#include <string>

namespace moderngekko
{
enum class DolphinShaderApi
{
  OpenGl,
  Vulkan,
  D3d,
  Metal,
};

struct DolphinShaderCapabilities
{
  bool dual_source_blend = true;
  bool geometry_shaders = true;
  bool primitive_restart = true;
  bool early_z = true;
  bool binding_layout = true;
  bool bounding_box = false;
  bool geometry_shader_instancing = true;
  bool clip_control = true;
  bool ssaa = true;
  bool fragment_stores_and_atomics = true;
  bool depth_clamp = true;
  bool reversed_depth_range = true;
  bool bitfield = true;
  bool dynamic_sampler_indexing = true;
  bool framebuffer_fetch = false;
  bool logic_op = true;
  bool palette_conversion = true;
  bool lod_bias_in_sampler = true;
  bool dynamic_vertex_loader = false;
  bool vertex_line_point_expand = false;
  bool gl_layer_in_fragment_shader = true;
  bool texture_query_levels = true;
  bool coarse_derivatives = true;
};

struct DolphinShaderOptions
{
  bool fast_depth = true;
  bool pixel_lighting = false;
  bool force_true_color = false;
  bool wireframe = false;
  bool bounding_box = false;
  bool ssaa = false;
  bool stereo = false;
  bool validation_layer = false;
  bool fast_texture_sampling = true;
  bool vertex_rounding = false;
  std::uint32_t multisamples = 1;
  std::uint32_t efb_scale = 1;
};

struct DolphinShaderBundle
{
  std::string vertex;
  std::string pixel;
  std::string geometry;
  std::string uber_vertex;
  std::string uber_pixel;
  std::uint64_t vertex_uid = 0;
  std::uint64_t pixel_uid = 0;
  std::uint64_t geometry_uid = 0;
  std::uint64_t uber_vertex_uid = 0;
  std::uint64_t uber_pixel_uid = 0;
};

class DolphinShaderCompiler final
{
public:
  static void SetCacheDirectory(std::string directory);
  static DolphinShaderBundle Compile(const GxStateView& state, GxTopology topology,
                                     std::uint8_t vat, DolphinShaderApi api,
                                     const DolphinShaderCapabilities& capabilities = {},
                                     const DolphinShaderOptions& options = {});
};
}
