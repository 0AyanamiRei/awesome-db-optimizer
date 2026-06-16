#pragma once

#include "volcano/stats.hpp"

#include <string>

namespace volcano {

class DuckDBSqlFrontend {
public:
  static JoinGraph ParseJoinGraph(const std::string &sql, const StatsCatalog &stats);
};

} // namespace volcano
