#pragma once

#include "volcano/search_strategy.hpp"

#include <unordered_map>
#include <string>
#include <utility>

namespace volcano {

// System-R style bottom-up dynamic programming.
// Fills a DP table from small relation sets to large ones.
// No duplicates, no cache hits (everything is pre-computed).
class BottomUpDP : public SearchStrategy {
public:
  std::string Name() const override { return "BottomUpDP"; }
  SearchResult Search(const JoinGraph &graph,
                      const StatsCatalog &stats,
                      const RequiredProperty &property) override;

private:
  // DP table key: (RelSet, RequiredProperty) -> best plan
  using CacheKey = std::pair<RelSet, std::string>;
  using CacheTable = std::unordered_map<std::string, PlanPtr>;

  std::string MakeKey(RelSet relset, const RequiredProperty &property) const;

  PlanPtr OptimizeAny(CacheTable &cache, const JoinGraph &graph,
                      RelSet relset, SearchTrace &trace);
  PlanPtr OptimizeSorted(CacheTable &cache, const JoinGraph &graph,
                         RelSet relset, const ColumnRef &sort_key,
                         SearchTrace &trace);
  void FillRelation(CacheTable &cache, const JoinGraph &graph,
                    RelSet singleton, const Relation &relation,
                    SearchTrace &trace);
};

} // namespace volcano
