#pragma once

#include "volcano/types.hpp"
#include "volcano/stats.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace volcano {

// Unified, cross-strategy comparable trace counters
struct SearchTrace {
  // Enumeration counters (all strategies)
  uint64_t partitions_explored = 0;   // how many (L,R) splits were considered
  uint64_t partitions_rejected = 0;   // rejected by connectivity / CP-free check
  uint64_t plans_costed = 0;          // physical plans actually costed
  uint64_t cache_hits = 0;            // memoization cache hits
  uint64_t plans_cached = 0;          // unique (RelSet, Property) entries cached

  // Strategy-specific (0 = not applicable for that strategy)
  uint64_t duplicates_generated = 0;  // transformational: duplicate expressions
  uint64_t rule_applications = 0;     // transformational: rule application attempts
  uint64_t dp_cells_filled = 0;       // bottom-up: DP table entries filled
  uint64_t branches_pruned = 0;       // top-down: branch-and-bound prunes

  double best_cost = 0.0;
};

struct SearchResult {
  bool has_plan = false;
  PhysicalPlan best_plan;
  SearchTrace trace;
};

// Abstract search strategy interface
class SearchStrategy {
public:
  virtual ~SearchStrategy() = default;

  // Human-readable name for reporting, e.g. "DPSub", "Transformational", "TopDown(Naive)"
  virtual std::string Name() const = 0;

  // Run the search on a join graph with the given stats and required root property.
  virtual SearchResult Search(const JoinGraph &graph,
                              const StatsCatalog &stats,
                              const RequiredProperty &property) = 0;
};

} // namespace volcano
