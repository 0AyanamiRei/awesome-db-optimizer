#pragma once

#include "volcano/search_strategy.hpp"

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
};

} // namespace volcano
