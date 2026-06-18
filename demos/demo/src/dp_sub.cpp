#include "volcano/dp_sub.hpp"
#include "volcano/cost_model.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace volcano {
namespace {

std::size_t PopCount(RelSet set) {
  std::size_t count = 0;
  while (set != 0) {
    set &= set - 1;
    ++count;
  }
  return count;
}

void TryUpdate(PlanPtr &best, PlanPtr candidate) {
  if (Better(candidate, best)) {
    best = std::move(candidate);
  }
}

} // namespace

std::string DPSub::MakeKey(RelSet relset, const RequiredProperty &property) const {
  std::ostringstream out;
  out << relset << "|" << property.ToString();
  return out.str();
}

std::vector<std::vector<RelSet>> DPSub::CollectConnectedSubsets(
    const JoinGraph &graph) const {
  const auto n = graph.RelationCount();
  const RelSet full = graph.FullSet();

  // connected_by_size[i] = vector of connected RelSets of size i
  std::vector<std::vector<RelSet>> connected_by_size(n + 1);

  // Enumerate all non-empty subsets, check connectivity, bucket by size.
  // O(2^n) scan — adequate for demo scale (n ≤ 10).
  for (RelSet s = 1; s <= full; ++s) {
    if ((s & ~full) != 0) break; // safety: stay within valid bits
    if (!graph.IsConnected(s)) continue;
    const auto size = PopCount(s);
    connected_by_size[size].push_back(s);
  }

  return connected_by_size;
}

SearchResult DPSub::Search(const JoinGraph &graph,
                           const StatsCatalog &stats,
                           const RequiredProperty &property) {
  SearchTrace trace;
  CacheTable cache;

  const auto full = graph.FullSet();
  if (full == 0) {
    SearchResult result;
    result.trace = trace;
    return result;
  }

  // ──────────────────────────────────────────────
  // Step 1: Seed singletons (Algorithm 1, Lines 1-3)
  // ──────────────────────────────────────────────
  for (const auto &relation : graph.Relations()) {
    const RelSet singleton = RelSet{1} << relation.id;
    FillRelation(cache, graph, singleton, relation, stats, trace);
  }

  // ──────────────────────────────────────────────
  // Step 2: Pre-compute connected subsets by size
  //   Si = {S | S ⊆ R and |S| = i and S is connected}
  //   (Algorithm 1, Line 5)
  // ──────────────────────────────────────────────
  const auto connected_by_size = CollectConnectedSubsets(graph);
  const std::size_t n = graph.RelationCount();

  // ──────────────────────────────────────────────
  // Step 3: DP by increasing size (Algorithm 1, Lines 4-25)
  // ──────────────────────────────────────────────
  for (std::size_t i = 2; i <= n; ++i) {
    const auto &Si = connected_by_size[i]; // connected subsets of size i

    for (RelSet S : Si) {
      // ── Enumerate all proper subsets S_left ⊂ S (Algorithm 1, Line 8) ──
      // This loop is trivially parallelizable — each S_left is independent,
      // and BestPlan(S) updates can be deferred / done with atomic compare.
      for (RelSet S_left = (S - 1) & S; S_left != 0; S_left = (S_left - 1) & S) {
        ++trace.partitions_explored;
        const RelSet S_right = S ^ S_left; // S \ S_left

        // ── CCP Block (Algorithm 1, Lines 12-17) ──
        // (1) Both non-empty — guaranteed by the subset enumeration
        //     (S_left ranges over non-empty proper subsets)
        // (2) S_left is connected
        if (!graph.IsConnected(S_left)) {
          ++trace.partitions_rejected;
          continue;
        }
        // (3) S_right is connected
        if (!graph.IsConnected(S_right)) {
          ++trace.partitions_rejected;
          continue;
        }
        // (4) Disjoint — guaranteed by S_right = S \ S_left
        // (5) Edge exists between S_left and S_right
        if (!graph.HasPredicateAcross(S_left, S_right)) {
          ++trace.partitions_rejected;
          continue;
        }

        // ── Valid CCP-Pair → create physical plans (Algorithm 1, Lines 19-22) ──
        const auto predicates = graph.CrossingPredicates(S_left, S_right);
        const auto *predicate = predicates.empty() ? nullptr : &predicates.front();

        // --- Optimize for Any property ---
        {
          const auto left_key = MakeKey(S_left, RequiredProperty::Any());
          const auto right_key = MakeKey(S_right, RequiredProperty::Any());
          auto left_plan = cache.find(left_key);
          auto right_plan = cache.find(right_key);
          if (left_plan != cache.end() && left_plan->second &&
              right_plan != cache.end() && right_plan->second) {

            // Hash join
            auto hash_join = MakeJoin(PhysicalOp::HashJoin,
                                       left_plan->second, right_plan->second,
                                       graph, stats, predicate, RequiredProperty::Any());
            ++trace.plans_costed;
            TryUpdate(cache[MakeKey(S, RequiredProperty::Any())], hash_join);

            // Nested-loop join
            auto nested_loop = MakeJoin(PhysicalOp::NestedLoopJoin,
                                         left_plan->second, right_plan->second,
                                         graph, stats, predicate, RequiredProperty::Any());
            ++trace.plans_costed;
            TryUpdate(cache[MakeKey(S, RequiredProperty::Any())], nested_loop);

            // Merge join under Any: request Sorted children
            if (predicate != nullptr) {
              const bool left_has_pred_left =
                  (S_left & graph.MaskForAlias(predicate->left.alias)) != 0;
              const auto left_sort_key =
                  left_has_pred_left ? predicate->left : predicate->right;
              const auto right_sort_key =
                  left_has_pred_left ? predicate->right : predicate->left;

              auto sorted_left =
                  cache.find(MakeKey(S_left, RequiredProperty::Sorted(left_sort_key)));
              auto sorted_right =
                  cache.find(MakeKey(S_right, RequiredProperty::Sorted(right_sort_key)));
              if (sorted_left != cache.end() && sorted_left->second &&
                  sorted_right != cache.end() && sorted_right->second) {
                auto merge_join = MakeJoin(PhysicalOp::MergeJoin,
                                            sorted_left->second, sorted_right->second,
                                            graph, stats, predicate, RequiredProperty::Any());
                ++trace.plans_costed;
                TryUpdate(cache[MakeKey(S, RequiredProperty::Any())], merge_join);
              }
            }
          }
        }

        // --- Optimize for Sorted properties via matching predicates ---
        for (const auto &pred : predicates) {
          for (int side = 0; side < 2; ++side) {
            const auto sort_key = (side == 0) ? pred.left : pred.right;
            const bool key_in_left =
                (S_left & graph.MaskForAlias(sort_key.alias)) != 0;
            const auto left_sort_key =
                key_in_left ? sort_key : (side == 0 ? pred.right : pred.left);
            const auto right_sort_key =
                key_in_left ? (side == 0 ? pred.right : pred.left) : sort_key;

            auto sorted_left =
                cache.find(MakeKey(S_left, RequiredProperty::Sorted(left_sort_key)));
            auto sorted_right =
                cache.find(MakeKey(S_right, RequiredProperty::Sorted(right_sort_key)));
            if (sorted_left == cache.end() || !sorted_left->second ||
                sorted_right == cache.end() || !sorted_right->second) {
              continue;
            }

            auto merge_join = MakeJoin(PhysicalOp::MergeJoin,
                                        sorted_left->second, sorted_right->second,
                                        graph, stats, &pred, RequiredProperty::Sorted(sort_key));
            ++trace.plans_costed;
            TryUpdate(cache[MakeKey(S, RequiredProperty::Sorted(sort_key))], merge_join);
          }
        }
      }

      // ── Sort enforcer propagation (interesting orders) ──
      // For each S, add SortEnforcer on top of Any plan for columns that
      // appear in join predicates — they may be useful for a parent MergeJoin.
      {
        auto any_it = cache.find(MakeKey(S, RequiredProperty::Any()));
        if (any_it != cache.end() && any_it->second) {
          for (const auto &relation : graph.Relations()) {
            if ((S & (RelSet{1} << relation.id)) == 0) continue;
            for (const auto &pred : graph.Predicates()) {
              if (pred.left.alias == relation.alias) {
                auto sorted = MakeSort(any_it->second, pred.left);
                ++trace.plans_costed;
                TryUpdate(cache[MakeKey(S, RequiredProperty::Sorted(pred.left))], sorted);
              }
              if (pred.right.alias == relation.alias) {
                auto sorted = MakeSort(any_it->second, pred.right);
                ++trace.plans_costed;
                TryUpdate(cache[MakeKey(S, RequiredProperty::Sorted(pred.right))], sorted);
              }
            }
          }
        }
      }

      trace.dp_cells_filled = cache.size();
    }
  }

  // ──────────────────────────────────────────────
  // Collect results
  // ──────────────────────────────────────────────
  SearchResult result;
  const auto key = MakeKey(full, property);
  PlanPtr best;
  auto it = cache.find(key);
  if (it != cache.end() && it->second) {
    best = it->second;
  }

  // A root Sorted property can request a column that is not an interesting
  // join order. In that case, satisfy it by sorting the best Any plan.
  if (!property.IsAny()) {
    auto any_it = cache.find(MakeKey(full, RequiredProperty::Any()));
    if (any_it != cache.end() && any_it->second) {
      auto sorted = MakeSort(any_it->second, property.sort_key);
      ++trace.plans_costed;
      if (Better(sorted, best)) {
        best = sorted;
        cache[key] = sorted;
      }
    }
  }

  if (best) {
    result.has_plan = true;
    result.best_plan = *best;
    trace.best_cost = result.best_plan.cost;
  }
  trace.plans_cached = cache.size();
  result.trace = trace;
  return result;
}

void DPSub::FillRelation(CacheTable &cache, const JoinGraph &graph,
                          RelSet singleton, const Relation &relation,
                          const StatsCatalog &stats,
                          SearchTrace &trace) {
  auto scan = MakeScan(relation, stats);
  ++trace.plans_costed;
  cache[MakeKey(singleton, RequiredProperty::Any())] = scan;

  // Also create Sorted plans via Sort enforcer for columns in predicates
  for (const auto &pred : graph.Predicates()) {
    ColumnRef key;
    if (pred.left.alias == relation.alias) {
      key = pred.left;
    } else if (pred.right.alias == relation.alias) {
      key = pred.right;
    } else {
      continue;
    }
    auto sorted = MakeSort(scan, key);
    ++trace.plans_costed;
    auto &entry = cache[MakeKey(singleton, RequiredProperty::Sorted(key))];
    if (Better(sorted, entry)) {
      entry = sorted;
    }
  }
  trace.dp_cells_filled = cache.size();
}

} // namespace volcano
