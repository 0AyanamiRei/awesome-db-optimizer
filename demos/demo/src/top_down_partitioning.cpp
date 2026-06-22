#include "volcano/top_down_partitioning.hpp"
#include "volcano/cost_model.hpp"
#include "volcano/mincut.hpp"
#include "volcano/search_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace volcano {

TopDownPartitioning::TopDownPartitioning(PartitionStrategy partition_strategy,
                                         bool allow_cross_products)
    : partition_strategy_(partition_strategy)
    , allow_cross_products_(allow_cross_products) {
}

TopDownPartitioning::~TopDownPartitioning() = default;

std::string TopDownPartitioning::Name() const {
  switch (partition_strategy_) {
  case PartitionStrategy::Naive:
    return "TopDown(Naive)";
  case PartitionStrategy::Mincut:
    return "TopDown(Mincut)";
  }
  return "TopDown(Unknown)";
}

SearchResult TopDownPartitioning::Search(const JoinGraph &graph,
                                          const StatsCatalog &stats,
                                          const RequiredProperty &property) {
  cache_.clear();
  SearchTrace trace;

  auto plan = BestPlan(graph, stats, graph.FullSet(), property, trace);

  SearchResult result;
  if (plan) {
    result.has_plan = true;
    result.best_plan = *plan;
  }
  result.trace = trace;
  result.trace.best_cost = plan ? plan->cost : 0.0;
  result.trace.plans_cached = cache_.size();
  return result;
}

PlanPtr TopDownPartitioning::BestPlan(const JoinGraph &graph,
                                       const StatsCatalog &stats,
                                       RelSet relset,
                                       const RequiredProperty &property,
                                       SearchTrace &trace) {
  // --- Memoization ---
  const auto key = detail::MakeMemoKey(relset, property);
  {
    const auto cached = cache_.find(key);
    if (cached != cache_.end()) {
      ++trace.cache_hits;
      return cached->second;
    }
  }

  // --- Base case: single relation ---
  if (detail::PopCount(relset) == 1) {
    PlanPtr result;
    // Find the relation
    for (const auto &relation : graph.Relations()) {
      if ((relset & (RelSet{1} << relation.id)) == 0) continue;

      auto scan = MakeScan(relation, stats);
      ++trace.plans_costed;

      if (property.IsAny()) {
        result = scan;
      } else {
        // Check if this relation can produce the sorted property
        if (property.sort_key.alias == relation.alias) {
          result = MakeSort(scan, property.sort_key);
          ++trace.plans_costed;
        }
        // Otherwise, result stays nullptr (can't satisfy sort)
      }
      break;
    }
    cache_[key] = result;
    return result;
  }

  // --- Recursive case: enumerate partitions ---
  const auto partitions = EnumeratePartitions(graph, relset, trace);

  PlanPtr best;

  for (const auto &[left_set, right_set] : partitions) {
    // Try all physical join methods for this partition
    auto candidate = TryJoinMethods(graph, stats, left_set, right_set, property, best, trace);
    if (Better(candidate, best)) {
      best = candidate;
    }
  }

  // SortEnforcer fallback for Sorted property: best Any plan + Sort
  if (!property.IsAny()) {
    auto any_plan = BestPlan(graph, stats, relset, RequiredProperty::Any(), trace);
    if (any_plan) {
      auto sorted = MakeSort(any_plan, property.sort_key);
      ++trace.plans_costed;
      if (Better(sorted, best)) {
        best = sorted;
      }
    }
  }

  cache_[key] = best;
  return best;
}

std::vector<TopDownPartitioning::Partition>
TopDownPartitioning::EnumeratePartitions(const JoinGraph &graph, RelSet relset,
                                          SearchTrace &trace) const {
  std::vector<Partition> result;

  switch (partition_strategy_) {
  case PartitionStrategy::Naive: {
    // Enumerate all non-empty proper subsets, filter by connectivity
    for (RelSet left = (relset - 1) & relset; left != 0; left = (left - 1) & relset) {
      const RelSet right = relset ^ left;
      // Avoid processing both (L,R) and (R,L) — only process each unordered pair once
      if (left >= right) continue;

      ++trace.partitions_explored;
      if (!graph.IsValidJoinSplit(left, right, allow_cross_products_)) {
        ++trace.partitions_rejected;
        continue;
      }
      result.push_back({left, right});
    }
    break;
  }
  case PartitionStrategy::Mincut: {
    // Lazy-initialize the MincutPartitioner (once per graph).
    if (!mincut_partitioner_ || mincut_graph_ != &graph) {
      mincut_partitioner_ = std::make_unique<MincutPartitioner>(graph);
      mincut_graph_ = &graph;
    }

    // MincutLazy generates only CP-free partitions directly — no
    // generate-and-test.  Each returned pair already satisfies:
    //   both sides connected, left < right, crossing predicate exists.
    const auto partitions = mincut_partitioner_->EnumeratePartitions(relset);
    trace.partitions_explored += partitions.size();
    for (const auto &[left, right] : partitions) {
      result.push_back({left, right});
    }
    break;
  }
  }

  return result;
}

PlanPtr TopDownPartitioning::TryJoinMethods(const JoinGraph &graph,
                                             const StatsCatalog &stats,
                                             RelSet left_set, RelSet right_set,
                                             const RequiredProperty &required_prop,
                                             PlanPtr current_best,
                                             SearchTrace &trace) {
  const auto predicates = graph.CrossingPredicates(left_set, right_set);
  const auto *predicate = predicates.empty() ? nullptr : &predicates.front();
  PlanPtr best = current_best;

  // --- Predicted-cost branch-and-bound ---
  // Use sum of child lower bounds as a conservative pruning threshold.
  auto lb_left = LowerBound(graph, stats, left_set, RequiredProperty::Any());
  auto lb_right = LowerBound(graph, stats, right_set, RequiredProperty::Any());
  double predicted_lower = lb_left + lb_right + 1.0; // at least 1 unit for join
  if (best && predicted_lower >= best->cost) {
    ++trace.branches_pruned;
    return best;
  }

  // --- Any property: try HashJoin, NestedLoop, MergeJoin ---
  if (required_prop.IsAny()) {
    auto left_any = BestPlan(graph, stats, left_set, RequiredProperty::Any(), trace);
    auto right_any = BestPlan(graph, stats, right_set, RequiredProperty::Any(), trace);
    if (!left_any || !right_any) return best;

    auto hash_join = MakeJoin(PhysicalOp::HashJoin, left_any, right_any,
                               graph, stats, predicate, RequiredProperty::Any());
    ++trace.plans_costed;
    if (Better(hash_join, best)) best = hash_join;

    auto nested_loop = MakeJoin(PhysicalOp::NestedLoopJoin, left_any, right_any,
                                 graph, stats, predicate, RequiredProperty::Any());
    ++trace.plans_costed;
    if (Better(nested_loop, best)) best = nested_loop;

    // MergeJoin requires sorted children
    if (predicate != nullptr) {
      const bool left_has_pred_left = (left_set & graph.MaskForAlias(predicate->left.alias)) != 0;
      const auto left_key = left_has_pred_left ? predicate->left : predicate->right;
      const auto right_key = left_has_pred_left ? predicate->right : predicate->left;

      auto sorted_left = BestPlan(graph, stats, left_set, RequiredProperty::Sorted(left_key), trace);
      auto sorted_right = BestPlan(graph, stats, right_set, RequiredProperty::Sorted(right_key), trace);
      if (sorted_left && sorted_right) {
        auto merge_join = MakeJoin(PhysicalOp::MergeJoin, sorted_left, sorted_right,
                                    graph, stats, predicate, RequiredProperty::Any());
        ++trace.plans_costed;
        if (Better(merge_join, best)) best = merge_join;
      }
    }
    return best;
  }

  // --- Sorted property: try MergeJoin with predicate matching the sort key ---
  const auto &sort_key = required_prop.sort_key;
  for (const auto &pred : predicates) {
    const bool sort_on_left = pred.left == sort_key;
    const bool sort_on_right = pred.right == sort_key;
    if (!sort_on_left && !sort_on_right) continue;

    const bool key_from_left_input = (left_set & graph.MaskForAlias(sort_key.alias)) != 0;
    const auto other_key = sort_on_left ? pred.right : pred.left;
    auto sorted_left = BestPlan(graph, stats, left_set,
                                 RequiredProperty::Sorted(key_from_left_input ? sort_key : other_key),
                                 trace);
    auto sorted_right = BestPlan(graph, stats, right_set,
                                  RequiredProperty::Sorted(key_from_left_input ? other_key : sort_key),
                                  trace);
    if (sorted_left && sorted_right) {
      auto merge_join = MakeJoin(PhysicalOp::MergeJoin, sorted_left, sorted_right,
                                  graph, stats, &pred, RequiredProperty::Sorted(sort_key));
      ++trace.plans_costed;
      if (Better(merge_join, best)) best = merge_join;
    }
  }

  return best;
}

double TopDownPartitioning::LowerBound(const JoinGraph &graph, const StatsCatalog &stats, RelSet relset,
                                        const RequiredProperty &property) const {
  // Simple lower bound: sum of base scan costs for all relations in the set.
  // This is always ≤ actual best plan cost.
  double lb = 0.0;
  for (const auto &relation : graph.Relations()) {
    if ((relset & (RelSet{1} << relation.id)) != 0) {
      lb += stats.LookupRelation(relation.alias).scan_cost;
    }
  }
  (void)property; // property doesn't affect our simple lower bound
  return lb;
}

} // namespace volcano
