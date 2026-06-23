#include "volcano/dp_sub.hpp"
#include "volcano/search_helpers.hpp"

#include <string>
#include <unordered_map>

namespace volcano {

SearchResult DPSub::Search(const JoinGraph &graph,
                           const StatsCatalog &stats,
                           const RequiredProperty &property) {
  using CacheTable = std::unordered_map<std::string, PlanPtr>;

  SearchTrace trace;
  CacheTable cache;

  const auto full = graph.FullSet();
  if (full == 0) {
    SearchResult result;
    result.trace = trace;
    return result;
  }

  for (const auto &relation : graph.Relations()) {
    const RelSet singleton = RelSet{1} << relation.id;
    detail::FillRelation(cache, graph, singleton, relation, stats, trace);
  }

  const auto connected_by_size = detail::CollectConnectedSubsets(graph);
  const std::size_t n = graph.RelationCount();

  for (std::size_t i = 2; i <= n; ++i) {
    const auto &Si = connected_by_size[i];
    for (RelSet S : Si) {
      for (RelSet S_left = (S - 1) & S; S_left != 0; S_left = (S_left - 1) & S) {
        ++trace.partitions_explored;
        const RelSet S_right = S ^ S_left;

        if (!graph.IsConnected(S_left)) {
          ++trace.partitions_rejected;
          continue;
        }
        if (!graph.IsConnected(S_right)) {
          ++trace.partitions_rejected;
          continue;
        }
        if (!graph.HasPredicateAcross(S_left, S_right)) {
          ++trace.partitions_rejected;
          continue;
        }

        detail::BuildPhysicalPlansForPair(cache, graph, stats, S, S_left, S_right, trace);
      }

      detail::AddSortEnforcers(cache, graph, S, trace);
      trace.dp_cells_filled = cache.size();
    }
  }

  SearchResult result;
  const auto key = detail::MakeMemoKey(full, property);
  PlanPtr best;
  auto it = cache.find(key);
  if (it != cache.end() && it->second) {
    best = it->second;
  }

  if (!property.IsAny()) {
    auto any_it = cache.find(detail::MakeMemoKey(full, RequiredProperty::Any()));
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

} // namespace volcano
