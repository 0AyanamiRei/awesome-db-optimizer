#pragma once

#include "volcano/search_strategy.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace volcano {

// DPSUB: Dynamic Programming Subset-driven join enumeration.
//
// Implements Algorithm 1 ("Generic DPSUB") from:
//   Mancini et al., "Efficient Massively Parallel Join Optimization
//   for Large Queries", SIGMOD 2022.
//
// Cross-referenced with:
//   Moerkotte & Neumann, "Analysis of Two Existing and One New
//   Dynamic Programming Algorithm for the Generation of Optimal
//   Bushy Join Trees without Cross Products", VLDB 2006.
//
// Key characteristics:
//   - Enumerates connected subsets by increasing size.
//   - For each connected subset S, enumerates all proper subsets S_left ⊂ S,
//     then checks the CCP block (connected, disjoint, edge between).
//   - No symmetry shortcut — both (S_left, S_right) and (S_right, S_left)
//     are evaluated, matching Algorithm 1 exactly.
//   - Inner loop over S_left is trivially parallelizable.
class DPSub : public SearchStrategy {
public:
  std::string Name() const override { return "DPSub"; }
  SearchResult Search(const JoinGraph &graph,
                      const StatsCatalog &stats,
                      const RequiredProperty &property) override;

private:
  // DP table key: (RelSet, RequiredProperty) -> best plan
  using CacheTable = std::unordered_map<std::string, PlanPtr>;

  std::string MakeKey(RelSet relset, const RequiredProperty &property) const;

  // Collect all connected subsets, bucketed by size.
  // connected_by_size[0] and connected_by_size[1] are populated for
  // convenience (size-0 is empty, size-1 has singletons).
  std::vector<std::vector<RelSet>> CollectConnectedSubsets(const JoinGraph &graph) const;

  // Seed singleton plans (size-1 base cases).
  void FillRelation(CacheTable &cache, const JoinGraph &graph,
                    RelSet singleton, const Relation &relation,
                    const StatsCatalog &stats,
                    SearchTrace &trace);
};

} // namespace volcano
