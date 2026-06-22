#pragma once

#include "volcano/search_strategy.hpp"

#include <vector>

namespace volcano {

// MPDP: Massively Parallel Dynamic Programming candidate enumeration.
//
// This demo implements the SIGMOD 2022 MPDP general-graph idea in the same
// bottom-up DP framework as DPSub.  For each connected DP state S, MPDP finds
// the blocks of the induced graph G[S], enumerates block-local CCP pairs
// (lb, rb), then grows lb inside S \ rb to obtain a global Join-Pair.
//
// The cost model, physical operators, interesting-order propagation, and memo
// table contract intentionally match DPSub so strategy comparisons isolate the
// candidate-enumeration difference.
class MPDP : public SearchStrategy {
public:
  std::string Name() const override { return "MPDP"; }
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
};

} // namespace volcano
