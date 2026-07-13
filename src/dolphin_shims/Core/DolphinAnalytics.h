#pragma once

#include <string_view>

namespace Common
{
class AnalyticsReportBuilder
{
public:
  AnalyticsReportBuilder() = default;
  template <typename T>
  void AddData(std::string_view, T&&)
  {
  }
};
}

class DolphinAnalytics
{
public:
  static DolphinAnalytics& Instance()
  {
    static DolphinAnalytics analytics;
    return analytics;
  }

  Common::AnalyticsReportBuilder BaseBuilder() const { return {}; }
  void Send(const Common::AnalyticsReportBuilder&) {}
};
