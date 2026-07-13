#include "moderngekko/dolphin_shader_compiler.hpp"

#include "VideoCommon/BPMemory.h"
#include "VideoCommon/GeometryShaderGen.h"
#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/PixelShaderGen.h"
#include "VideoCommon/UberShaderPixel.h"
#include "VideoCommon/UberShaderVertex.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <span>

namespace moderngekko
{
void SetDolphinShaderCacheDirectory(std::string directory);

namespace
{
std::mutex s_shader_mutex;

APIType ConvertApi(DolphinShaderApi api)
{
  switch (api)
  {
  case DolphinShaderApi::OpenGl: return APIType::OpenGL;
  case DolphinShaderApi::Vulkan: return APIType::Vulkan;
  case DolphinShaderApi::D3d: return APIType::D3D;
  case DolphinShaderApi::Metal: return APIType::Metal;
  }
  return APIType::Nothing;
}

PrimitiveType ConvertTopology(GxTopology topology)
{
  switch (topology)
  {
  case GxTopology::Triangles: return PrimitiveType::Triangles;
  case GxTopology::Lines: return PrimitiveType::Lines;
  case GxTopology::Points: return PrimitiveType::Points;
  }
  return PrimitiveType::Triangles;
}

template <typename Uid>
std::uint64_t HashUid(const Uid& uid)
{
  std::uint64_t hash = 14695981039346656037ull;
  for (const std::uint8_t value :
       std::span{uid.GetUidDataRaw(), uid.GetUidDataSize()})
  {
    hash ^= value;
    hash *= 1099511628211ull;
  }
  return hash;
}

void LoadState(const GxStateView& state)
{
  static_assert(sizeof(BPMemory) == 256u * sizeof(std::uint32_t));
  static_assert(sizeof(XFMemory) == 0x1058u * sizeof(std::uint32_t));
  std::memset(&bpmem, 0, sizeof(bpmem));
  std::memset(&xfmem, 0, sizeof(xfmem));
  std::memcpy(&bpmem, state.bp.data(),
              std::min(state.bp.size_bytes(), sizeof(bpmem)));
  std::memcpy(&xfmem, state.xf.data(),
              std::min(state.xf.size_bytes(), sizeof(xfmem)));
}

void LoadVertexComponents(const GxStateView& state, std::uint8_t vat)
{
  VertexLoaderManager::g_current_components = 0;
  if (state.cp.size() < 0x98u)
    return;
  const std::uint32_t low = state.cp[0x50u];
  const std::uint32_t high = state.cp[0x60u];
  VertexLoaderManager::g_current_components |= (low & 0x1FFu) << 1u;
  if (((low >> 11u) & 3u) != 0u)
  {
    VertexLoaderManager::g_current_components |= VB_HAS_NORMAL;
    if ((state.cp[0x70u + (vat & 7u)] & (1u << 9u)) != 0u)
      VertexLoaderManager::g_current_components |= VB_HAS_TANGENT | VB_HAS_BINORMAL;
  }
  if (((low >> 13u) & 3u) != 0u)
    VertexLoaderManager::g_current_components |= VB_HAS_COL0;
  if (((low >> 15u) & 3u) != 0u)
    VertexLoaderManager::g_current_components |= VB_HAS_COL1;
  for (unsigned index = 0; index < 8u; ++index)
  {
    if (((high >> (index * 2u)) & 3u) != 0u)
      VertexLoaderManager::g_current_components |= VB_HAS_UV0 << index;
  }
}

void LoadHostConfig(DolphinShaderApi api, const DolphinShaderCapabilities& caps,
                    const DolphinShaderOptions& options)
{
  g_Config = {};
  g_ActiveConfig = {};
  g_backend_info = {};
  g_backend_info.api_type = ConvertApi(api);
  g_backend_info.bSupportsDualSourceBlend = caps.dual_source_blend;
  g_backend_info.bSupportsGeometryShaders = caps.geometry_shaders;
  g_backend_info.bSupportsPrimitiveRestart = caps.primitive_restart;
  g_backend_info.bSupportsEarlyZ = caps.early_z;
  g_backend_info.bSupportsBindingLayout = caps.binding_layout;
  g_backend_info.bSupportsBBox = caps.bounding_box;
  g_backend_info.bSupportsGSInstancing = caps.geometry_shader_instancing;
  g_backend_info.bSupportsClipControl = caps.clip_control;
  g_backend_info.bSupportsSSAA = caps.ssaa;
  g_backend_info.bSupportsFragmentStoresAndAtomics = caps.fragment_stores_and_atomics;
  g_backend_info.bSupportsDepthClamp = caps.depth_clamp;
  g_backend_info.bSupportsReversedDepthRange = caps.reversed_depth_range;
  g_backend_info.bSupportsBitfield = caps.bitfield;
  g_backend_info.bSupportsDynamicSamplerIndexing = caps.dynamic_sampler_indexing;
  g_backend_info.bSupportsFramebufferFetch = caps.framebuffer_fetch;
  g_backend_info.bSupportsLogicOp = caps.logic_op;
  g_backend_info.bSupportsPaletteConversion = caps.palette_conversion;
  g_backend_info.bSupportsLodBiasInSampler = caps.lod_bias_in_sampler;
  g_backend_info.bSupportsDynamicVertexLoader = caps.dynamic_vertex_loader;
  g_backend_info.bSupportsVSLinePointExpand = caps.vertex_line_point_expand;
  g_backend_info.bSupportsGLLayerInFS = caps.gl_layer_in_fragment_shader;
  g_backend_info.bSupportsTextureQueryLevels = caps.texture_query_levels;
  g_backend_info.bSupportsCoarseDerivatives = caps.coarse_derivatives;
  g_ActiveConfig.bFastDepthCalc = options.fast_depth;
  g_ActiveConfig.bEnablePixelLighting = options.pixel_lighting;
  g_ActiveConfig.bForceTrueColor = options.force_true_color;
  g_ActiveConfig.bWireFrame = options.wireframe;
  g_ActiveConfig.bBBoxEnable = options.bounding_box;
  g_ActiveConfig.bSSAA = options.ssaa;
  g_ActiveConfig.stereo_mode = options.stereo ? StereoMode::SideBySide : StereoMode::Off;
  g_ActiveConfig.bEnableValidationLayer = options.validation_layer;
  g_ActiveConfig.bFastTextureSampling = options.fast_texture_sampling;
  g_ActiveConfig.bVertexRounding = options.vertex_rounding;
  g_ActiveConfig.iMultisamples = options.multisamples;
  g_ActiveConfig.iEFBScale = static_cast<int>(options.efb_scale);
}
}

void DolphinShaderCompiler::SetCacheDirectory(std::string directory)
{
  SetDolphinShaderCacheDirectory(std::move(directory));
}

DolphinShaderBundle DolphinShaderCompiler::Compile(
    const GxStateView& state, GxTopology topology, std::uint8_t vat, DolphinShaderApi api,
    const DolphinShaderCapabilities& capabilities, const DolphinShaderOptions& options)
{
  std::lock_guard lock{s_shader_mutex};
  LoadState(state);
  LoadVertexComponents(state, vat);
  LoadHostConfig(api, capabilities, options);
  const APIType dolphin_api = ConvertApi(api);
  const ShaderHostConfig host = ShaderHostConfig::GetCurrent();

  VertexShaderUid vertex_uid = GetVertexShaderUid();
  PixelShaderUid pixel_uid = GetPixelShaderUid();
  GeometryShaderUid geometry_uid = GetGeometryShaderUid(ConvertTopology(topology));
  UberShader::VertexShaderUid uber_vertex_uid = UberShader::GetVertexShaderUid();
  UberShader::PixelShaderUid uber_pixel_uid = UberShader::GetPixelShaderUid();

  DolphinShaderBundle bundle;
  bundle.vertex =
      GenerateVertexShaderCode(dolphin_api, host, vertex_uid.GetUidData(), {}).GetBuffer();
  bundle.pixel =
      GeneratePixelShaderCode(dolphin_api, host, pixel_uid.GetUidData(), {}).GetBuffer();
  bundle.geometry =
      GenerateGeometryShaderCode(dolphin_api, host, geometry_uid.GetUidData()).GetBuffer();
  bundle.uber_vertex =
      UberShader::GenVertexShader(dolphin_api, host, uber_vertex_uid.GetUidData()).GetBuffer();
  bundle.uber_pixel =
      UberShader::GenPixelShader(dolphin_api, host, uber_pixel_uid.GetUidData()).GetBuffer();
  bundle.vertex_uid = HashUid(vertex_uid);
  bundle.pixel_uid = HashUid(pixel_uid);
  bundle.geometry_uid = HashUid(geometry_uid);
  bundle.uber_vertex_uid = HashUid(uber_vertex_uid);
  bundle.uber_pixel_uid = HashUid(uber_pixel_uid);
  return bundle;
}
}
