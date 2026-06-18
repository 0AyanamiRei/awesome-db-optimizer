#pragma once

#include "volcano/stats.hpp"
#include "volcano/types.hpp"

#include <memory>

namespace volcano {

// Shared cost model and plan construction used by all strategies.
// All costs are deterministic, simple, teaching-oriented formulas.

// Create a SeqScan plan for a single relation (Any property).
PlanPtr MakeScan(const Relation &relation, const StatsCatalog &stats);

// Create a Sort enforcer on top of a child plan.
PlanPtr MakeSort(PlanPtr child, const ColumnRef &sort_key);

// Create a physical join plan.
// `op` must be HashJoin, NestedLoopJoin, or MergeJoin.
// `predicate` may be nullptr for cross-product joins.
PlanPtr MakeJoin(PhysicalOp op, PlanPtr left, PlanPtr right,
                 const JoinGraph &graph,
                 const StatsCatalog &stats,
                 const JoinPredicate *predicate,
                 const RequiredProperty &property);

// Best-plan comparison helper.
inline bool Better(const PlanPtr &candidate, const PlanPtr &best) {
  return candidate && (!best || candidate->cost < best->cost);
}

} // namespace volcano
