#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "VideoCommon/BoundingBox.h"
#include "VideoCommon/DriverDetails.h"
#include "VideoCommon/GraphicsModSystem/Config/GraphicsMod.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VertexLoaderManager.h"

#include <filesystem>
#include <memory>
#include <mutex>

VideoConfig g_Config;
VideoConfig g_ActiveConfig;
BackendInfo g_backend_info;
std::unique_ptr<BoundingBox> g_bounding_box;

namespace
{
std::mutex s_cache_path_mutex;
std::string s_cache_path;
}

namespace moderngekko
{
void SetDolphinShaderCacheDirectory(std::string directory)
{
  std::lock_guard lock{s_cache_path_mutex};
  if (!directory.empty() && directory.back() != '/' && directory.back() != '\\')
    directory.push_back(std::filesystem::path::preferred_separator);
  s_cache_path = std::move(directory);
}
}

GraphicsModGroupConfig::GraphicsModGroupConfig(std::string game_id)
    : m_game_id(std::move(game_id))
{
}
GraphicsModGroupConfig::~GraphicsModGroupConfig() = default;
GraphicsModGroupConfig::GraphicsModGroupConfig(const GraphicsModGroupConfig&) = default;
GraphicsModGroupConfig::GraphicsModGroupConfig(GraphicsModGroupConfig&&) noexcept = default;
GraphicsModGroupConfig& GraphicsModGroupConfig::operator=(const GraphicsModGroupConfig&) = default;
GraphicsModGroupConfig&
GraphicsModGroupConfig::operator=(GraphicsModGroupConfig&&) noexcept = default;

namespace Common::Log
{
void GenericLogFmtImpl(LogLevel, LogType, const char*, int, fmt::string_view,
                       const fmt::format_args&)
{
}
}

namespace Common
{
bool MsgAlertFmtImpl(bool, MsgType, Log::LogType, const char*, int, fmt::string_view,
                     const fmt::format_args&)
{
  return false;
}
}

namespace File
{
bool Exists(const std::string& path)
{
  std::error_code error;
  return std::filesystem::exists(path, error);
}

bool CreateDir(const std::string& path)
{
  std::error_code error;
  return std::filesystem::create_directories(path, error) ||
         std::filesystem::is_directory(path, error);
}

const std::string& GetUserPath(unsigned int)
{
  return s_cache_path;
}
}

namespace VertexLoaderManager
{
u32 g_current_components = 0;
}
