#include "volcano/bottom_up_dp.hpp"
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

std::string BottomUpDP::MakeKey(RelSet relset, const RequiredProperty &property) const {
  std::ostringstream out;
  out << relset << "|" << property.ToString();
  return out.str();
}

SearchResult BottomUpDP::Search(const JoinGraph &graph,
                                const StatsCatalog & /*stats*/,
                                const RequiredProperty &property) {
  SearchTrace trace;
  CacheTable cache;

  const auto full = graph.FullSet();
  if (full == 0) {
    SearchResult result;
    result.trace = trace;
    return result;
  }

  // Step 1: seed singletons
  for (const auto &relation : graph.Relations()) {
    const RelSet singleton = RelSet{1} << relation.id;
    FillRelation(cache, graph, singleton, relation, trace);
  }

  // Step 2: bottom-up by increasing size
  const std::size_t n = graph.RelationCount();
  for (std::size_t size = 2; size <= n; ++size) {
    for (RelSet set = 1; set != 0; ++set) {
      if ((set & ~full) != 0 || PopCount(set) != size) {
        if (set == full) break;
        continue;
      }

      // Enumerate all valid partitions of this set
      for (RelSet left = (set - 1) & set; left != 0; left = (left - 1) & set) {
        const RelSet right = set ^ left;
        if (left >= right) continue; // enumerate each unordered pair once
        if (!graph.IsValidJoinSplit(left, right, false)) {
          ++trace.partitions_rejected;
          continue;
        }
        ++trace.partitions_explored;

        // For each partition, try all join methods for both Any and Sorted properties
        const auto predicates = graph.CrossingPredicates(left, right);
        const auto *predicate = predicates.empty() ? nullptr : &predicates.front();

        // --- Optimize for Any property ---
        {
          const auto left_key = MakeKey(left, RequiredProperty::Any());
          const auto right_key = MakeKey(right, RequiredProperty::Any());
          auto left_plan = cache.find(left_key);
          auto right_plan = cache.find(right_key);
          if (left_plan == cache.end() || !left_plan->second ||
              right_plan == cache.end() || !right_plan->second) {
            continue;
          }

          auto hash_join = MakeJoin(PhysicalOp::HashJoin,
                                     left_plan->second, right_plan->second,
                                     graph, predicate, RequiredProperty::Any());
          ++trace.plans_costed;
          TryUpdate(cache[MakeKey(set, RequiredProperty::Any())], hash_join);

          auto nested_loop = MakeJoin(PhysicalOp::NestedLoopJoin,
                                       left_plan->second, right_plan->second,
                                       graph, predicate, RequiredProperty::Any());
          ++trace.plans_costed;
          TryUpdate(cache[MakeKey(set, RequiredProperty::Any())], nested_loop);

          // MergeJoin under Any: request Sorted children
          if (predicate != nullptr) {
            const bool left_has_pred_left = (left & graph.MaskForAlias(predicate->left.alias)) != 0;
            const auto left_sort_key = left_has_pred_left ? predicate->left : predicate->right;
            const auto right_sort_key = left_has_pred_left ? predicate->right : predicate->left;

            auto sorted_left = cache.find(MakeKey(left, RequiredProperty::Sorted(left_sort_key)));
            auto sorted_right = cache.find(MakeKey(right, RequiredProperty::Sorted(right_sort_key)));
            if (sorted_left != cache.end() && sorted_left->second &&
                sorted_right != cache.end() && sorted_right->second) {
              auto merge_join = MakeJoin(PhysicalOp::MergeJoin,
                                          sorted_left->second, sorted_right->second,
                                          graph, predicate, RequiredProperty::Any());
              ++trace.plans_costed;
              TryUpdate(cache[MakeKey(set, RequiredProperty::Any())], merge_join);
            }
          }
        }

        // --- Optimize for Sorted properties via matching predicates ---
        for (const auto &pred : predicates) {
          for (int side = 0; side < 2; ++side) {
            const auto sort_key = (side == 0) ? pred.left : pred.right;
            const bool key_in_left = (left & graph.MaskForAlias(sort_key.alias)) != 0;
            const auto left_sort_key = key_in_left ? sort_key : (side == 0 ? pred.right : pred.left);
            const auto right_sort_key = key_in_left ? (side == 0 ? pred.right : pred.left) : sort_key;

            auto sorted_left = cache.find(MakeKey(left, RequiredProperty::Sorted(left_sort_key)));
            auto sorted_right = cache.find(MakeKey(right, RequiredProperty::Sorted(right_sort_key)));
            if (sorted_left == cache.end() || !sorted_left->second ||
                sorted_right == cache.end() || !sorted_right->second) {
              continue;
            }

            auto merge_join = MakeJoin(PhysicalOp::MergeJoin,
                                        sorted_left->second, sorted_right->second,
                                        graph, &pred, RequiredProperty::Sorted(sort_key));
            ++trace.plans_costed;
            TryUpdate(cache[MakeKey(set, RequiredProperty::Sorted(sort_key))], merge_join);
          }
        }
      }

      // For each set, also add SortEnforcer on top of Any plan for Sorted properties.
      // This implements "interesting orders": any column that appears in a join
      // predicate can be useful for a parent MergeJoin.
      {
        auto any_it = cache.find(MakeKey(set, RequiredProperty::Any()));
        if (any_it != cache.end() && any_it->second) {
          for (const auto &relation : graph.Relations()) {
            if ((set & (RelSet{1} << relation.id)) == 0) continue;
            // Collect all predicate columns for this relation (not just the first).
            for (const auto &pred : graph.Predicates()) {
              if (pred.left.alias == relation.alias) {
                auto sorted = MakeSort(any_it->second, pred.left);
                ++trace.plans_costed;
                TryUpdate(cache[MakeKey(set, RequiredProperty::Sorted(pred.left))], sorted);
              }
              if (pred.right.alias == relation.alias) {
                auto sorted = MakeSort(any_it->second, pred.right);
                ++trace.plans_costed;
                TryUpdate(cache[MakeKey(set, RequiredProperty::Sorted(pred.right))], sorted);
              }
            }
          }
        }
      }

      trace.dp_cells_filled = cache.size();
      if (set == full) break;
    }
  }

  // Collect results
  SearchResult result;
  const auto key = MakeKey(full, property);
  auto it = cache.find(key);
  if (it != cache.end() && it->second) {
    result.best_plan = *it->second;
    result.trace = trace;
    result.trace.best_cost = result.best_plan.cost;
    result.trace.plans_cached = cache.size();
  }
  return result;
}

PlanPtr BottomUpDP::OptimizeAny(CacheTable &cache, const JoinGraph &graph,
                                 RelSet relset, SearchTrace &trace) {
  (void)cache; (void)graph; (void)relset; (void)trace;
  // Handled inline in Search() for efficiency.
  return nullptr;
}

PlanPtr BottomUpDP::OptimizeSorted(CacheTable &cache, const JoinGraph &graph,
                                    RelSet relset, const ColumnRef &sort_key,
                                    SearchTrace &trace) {
  (void)cache; (void)graph; (void)relset; (void)sort_key; (void)trace;
  // Handled inline in Search() for efficiency.
  return nullptr;
}

void BottomUpDP::FillRelation(CacheTable &cache, const JoinGraph &graph,
                               RelSet singleton, const Relation &relation,
                               SearchTrace &trace) {
  auto scan = MakeScan(relation);
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
