#pragma once

#include <string>

struct SConfig
{
  static SConfig& GetInstance()
  {
    static SConfig config;
    return config;
  }

  const std::string GetGameID() const { return m_game_id; }
  void SetGameID(std::string game_id) { m_game_id = std::move(game_id); }

private:
  std::string m_game_id = "00000000";
};
