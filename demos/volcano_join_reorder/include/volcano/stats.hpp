#pragma once

#include "volcano/types.hpp"

#include <string>
#include <unordered_map>

namespace volcano {

struct RelationStats {
  double rows = 0.0;
  double scan_cost = 0.0;
};

// Lightweight stats catalog for cost estimation.
// Can be constructed programmatically (AddRelationStats / AddSelectivity)
// or from JSON (FromJson / FromFile — kept for optional use).
class StatsCatalog {
public:
  // Programmatic construction (preferred for test cases).
  void AddRelationStats(const std::string &alias, RelationStats stats);
  void AddSelectivity(const ColumnRef &left, const ColumnRef &right, double selectivity);

  // JSON construction (optional, for backward compatibility).
  static StatsCatalog FromJson(const std::string &json);
  static StatsCatalog FromFile(const std::string &path);

  // Lookup.
  const RelationStats &LookupRelation(const std::string &alias) const;
  double LookupSelectivity(const ColumnRef &left, const ColumnRef &right) const;

private:
  static std::string PredicateKey(const ColumnRef &left, const ColumnRef &right);

  std::unordered_map<std::string, RelationStats> relations_;
  std::unordered_map<std::string, double> selectivities_;
};

} // namespace volcano
