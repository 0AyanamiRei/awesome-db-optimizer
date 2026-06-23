#pragma once

#include "volcano/search_strategy.hpp"

#include <cstddef>
#include <vector>

namespace volcano {

// MPDP: Massively Parallel Dynamic Programming candidate enumeration.
//
// This demo keeps the MPDP implementation focused on the paper's general-graph
// join-pair enumeration.  For each connected DP state S, MPDP finds the blocks
// of the induced graph G[S], enumerates block-local CCP pairs (lb, rb), then
// grows lb inside S \ rb to obtain a global Join-Pair.
//
// Unlike DPSub, the MPDP memo is relation-set only and does not propagate
// interesting orders through the DP table.  It costs the unordered physical
// join alternatives used by this demo (hash join and nested loop join) and, at
// the public SearchStrategy boundary, may add one root SortEnforcer to satisfy
// a requested Sorted(...) output property.
class MPDP : public SearchStrategy {
public:
  explicit MPDP(std::size_t thread_count = 1);

  std::string Name() const override;
  SearchResult Search(const JoinGraph &graph,
                      const StatsCatalog &stats,
                      const RequiredProperty &property) override;

private:
  struct JoinPair {
    RelSet left = 0;
    RelSet right = 0;
  };

  std::vector<JoinPair> EnumerateJoinPairs(const JoinGraph &graph, RelSet set) const;
  std::vector<RelSet> FindBlocks(const JoinGraph &graph, RelSet set) const;
  RelSet Grow(const JoinGraph &graph, RelSet seeds, RelSet allowed) const;

  std::size_t thread_count_ = 1;
};

} // namespace volcano
