#pragma once

#include "volcano/types.hpp"

#include <string>
#include <unordered_map>

namespace volcano {

struct RelationStats {
  double rows = 0.0;
  double scan_cost = 0.0;
};

class StatsCatalog {
public:
  static StatsCatalog FromJson(const std::string &json);
  static StatsCatalog FromFile(const std::string &path);

  const RelationStats &LookupRelation(const std::string &alias) const;
  double LookupSelectivity(const ColumnRef &left, const ColumnRef &right) const;

private:
  static std::string PredicateKey(const ColumnRef &left, const ColumnRef &right);

  std::unordered_map<std::string, RelationStats> relations_;
  std::unordered_map<std::string, double> selectivities_;
};

} // namespace volcano
