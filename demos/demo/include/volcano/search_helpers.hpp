#pragma once

#include "volcano/cost_model.hpp"
#include "volcano/search_strategy.hpp"

#include <bit>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace volcano::detail {

inline std::size_t PopCount(RelSet set) {
  return static_cast<std::size_t>(std::popcount(set));
}

inline std::size_t Ctz(RelSet set) {
  return static_cast<std::size_t>(std::countr_zero(set));
}

inline std::string MakeMemoKey(RelSet relset, const RequiredProperty &property) {
  std::ostringstream out;
  out << relset << "|" << property.ToString();
  return out.str();
}

template <typename PlanMap>
inline void TryUpdate(PlanMap &best, PlanPtr candidate) {
  if (Better(candidate, best)) {
    best = std::move(candidate);
  }
}

inline std::vector<std::vector<RelSet>> CollectConnectedSubsets(const JoinGraph &graph) {
  const auto n = graph.RelationCount();
  const RelSet full = graph.FullSet();
  std::vector<std::vector<RelSet>> connected_by_size(n + 1);

  for (RelSet s = 1; s <= full; ++s) {
    if ((s & ~full) != 0) break;
    if (!graph.IsConnected(s)) continue;
    connected_by_size[PopCount(s)].push_back(s);
  }

  return connected_by_size;
}

template <typename CacheTable>
inline void FillRelation(CacheTable &cache, const JoinGraph &graph,
                         RelSet singleton, const Relation &relation,
                         const StatsCatalog &stats,
                         SearchTrace &trace) {
  auto scan = MakeScan(relation, stats);
  ++trace.plans_costed;
  cache[MakeMemoKey(singleton, RequiredProperty::Any())] = scan;

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
    auto &entry = cache[MakeMemoKey(singleton, RequiredProperty::Sorted(key))];
    if (Better(sorted, entry)) {
      entry = sorted;
    }
  }
  trace.dp_cells_filled = cache.size();
}

template <typename CacheTable>
inline void BuildPhysicalPlansForPair(CacheTable &cache, const JoinGraph &graph,
                                      const StatsCatalog &stats,
                                      RelSet S, RelSet S_left, RelSet S_right,
                                      SearchTrace &trace) {
  const auto predicates = graph.CrossingPredicates(S_left, S_right);
  const auto *predicate = predicates.empty() ? nullptr : &predicates.front();

  {
    const auto left_key = MakeMemoKey(S_left, RequiredProperty::Any());
    const auto right_key = MakeMemoKey(S_right, RequiredProperty::Any());
    auto left_plan = cache.find(left_key);
    auto right_plan = cache.find(right_key);
    if (left_plan != cache.end() && left_plan->second &&
        right_plan != cache.end() && right_plan->second) {
      auto hash_join = MakeJoin(PhysicalOp::HashJoin,
                                left_plan->second, right_plan->second,
                                graph, stats, predicate, RequiredProperty::Any());
      ++trace.plans_costed;
      TryUpdate(cache[MakeMemoKey(S, RequiredProperty::Any())], hash_join);

      auto nested_loop = MakeJoin(PhysicalOp::NestedLoopJoin,
                                  left_plan->second, right_plan->second,
                                  graph, stats, predicate, RequiredProperty::Any());
      ++trace.plans_costed;
      TryUpdate(cache[MakeMemoKey(S, RequiredProperty::Any())], nested_loop);

      if (predicate != nullptr) {
        const bool left_has_pred_left =
            (S_left & graph.MaskForAlias(predicate->left.alias)) != 0;
        const auto left_sort_key =
            left_has_pred_left ? predicate->left : predicate->right;
        const auto right_sort_key =
            left_has_pred_left ? predicate->right : predicate->left;

        auto sorted_left =
            cache.find(MakeMemoKey(S_left, RequiredProperty::Sorted(left_sort_key)));
        auto sorted_right =
            cache.find(MakeMemoKey(S_right, RequiredProperty::Sorted(right_sort_key)));
        if (sorted_left != cache.end() && sorted_left->second &&
            sorted_right != cache.end() && sorted_right->second) {
          auto merge_join = MakeJoin(PhysicalOp::MergeJoin,
                                     sorted_left->second, sorted_right->second,
                                     graph, stats, predicate, RequiredProperty::Any());
          ++trace.plans_costed;
          TryUpdate(cache[MakeMemoKey(S, RequiredProperty::Any())], merge_join);
        }
      }
    }
  }

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
          cache.find(MakeMemoKey(S_left, RequiredProperty::Sorted(left_sort_key)));
      auto sorted_right =
          cache.find(MakeMemoKey(S_right, RequiredProperty::Sorted(right_sort_key)));
      if (sorted_left == cache.end() || !sorted_left->second ||
          sorted_right == cache.end() || !sorted_right->second) {
        continue;
      }

      auto merge_join = MakeJoin(PhysicalOp::MergeJoin,
                                 sorted_left->second, sorted_right->second,
                                 graph, stats, &pred, RequiredProperty::Sorted(sort_key));
      ++trace.plans_costed;
      TryUpdate(cache[MakeMemoKey(S, RequiredProperty::Sorted(sort_key))], merge_join);
    }
  }
}

template <typename CacheTable>
inline void AddSortEnforcers(CacheTable &cache, const JoinGraph &graph,
                             RelSet S, SearchTrace &trace) {
  auto any_it = cache.find(MakeMemoKey(S, RequiredProperty::Any()));
  if (any_it == cache.end() || !any_it->second) {
    return;
  }

  for (const auto &relation : graph.Relations()) {
    if ((S & (RelSet{1} << relation.id)) == 0) continue;
    for (const auto &pred : graph.Predicates()) {
      if (pred.left.alias == relation.alias) {
        auto sorted = MakeSort(any_it->second, pred.left);
        ++trace.plans_costed;
        TryUpdate(cache[MakeMemoKey(S, RequiredProperty::Sorted(pred.left))], sorted);
      }
      if (pred.right.alias == relation.alias) {
        auto sorted = MakeSort(any_it->second, pred.right);
        ++trace.plans_costed;
        TryUpdate(cache[MakeMemoKey(S, RequiredProperty::Sorted(pred.right))], sorted);
      }
    }
  }
}

} // namespace volcano::detail
