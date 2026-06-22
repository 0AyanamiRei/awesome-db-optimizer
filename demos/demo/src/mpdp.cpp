#include "volcano/mpdp.hpp"
#include "volcano/search_helpers.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace volcano {
namespace {

struct JoinPairKey {
  RelSet left = 0;
  RelSet right = 0;

  bool operator==(const JoinPairKey &other) const = default;
};

struct JoinPairKeyHash {
  std::size_t operator()(const JoinPairKey &key) const noexcept {
    const auto h1 = std::hash<RelSet>{}(key.left);
    const auto h2 = std::hash<RelSet>{}(key.right);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
  }
};

} // namespace

SearchResult MPDP::Search(const JoinGraph &graph,
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
      const auto join_pairs = EnumerateJoinPairs(graph, S);
      for (const auto &[S_left, S_right] : join_pairs) {
        ++trace.partitions_explored;
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

std::vector<MPDP::JoinPair> MPDP::EnumerateJoinPairs(const JoinGraph &graph,
                                                     RelSet set) const {
  std::vector<JoinPair> result;
  std::unordered_set<JoinPairKey, JoinPairKeyHash> emitted;

  for (RelSet block : FindBlocks(graph, set)) {
    for (RelSet lb = (block - 1) & block; lb != 0; lb = (lb - 1) & block) {
      const RelSet rb = block ^ lb;
      if (!graph.IsConnected(lb) || !graph.IsConnected(rb)) continue;
      if (!graph.HasPredicateAcross(lb, rb)) continue;

      const RelSet left = Grow(graph, lb, set & ~rb);
      const RelSet right = set ^ left;
      if (!graph.IsConnected(left) || !graph.IsConnected(right)) continue;
      if (!graph.HasPredicateAcross(left, right)) continue;

      const JoinPairKey key{left, right};
      if (emitted.insert(key).second) {
        result.push_back({left, right});
      }
    }
  }

  return result;
}

std::vector<RelSet> MPDP::FindBlocks(const JoinGraph &graph, RelSet set) const {
  std::vector<RelSet> blocks;
  const auto n = graph.RelationCount();
  if (detail::PopCount(set) <= 1) {
    if (set != 0) blocks.push_back(set);
    return blocks;
  }

  std::vector<RelSet> adjacency(n, 0);
  for (const auto &pred : graph.Predicates()) {
    const auto l = graph.RelationByAlias(pred.left.alias).id;
    const auto r = graph.RelationByAlias(pred.right.alias).id;
    const RelSet l_bit = RelSet{1} << l;
    const RelSet r_bit = RelSet{1} << r;
    if ((set & l_bit) == 0 || (set & r_bit) == 0) continue;
    adjacency[l] |= r_bit;
    adjacency[r] |= l_bit;
  }

  std::vector<int> disc(n, -1);
  std::vector<int> low(n, -1);
  std::vector<std::pair<std::size_t, std::size_t>> edge_stack;
  int time = 0;

  auto dfs = [&](auto &self, std::size_t u, std::size_t parent) -> void {
    disc[u] = low[u] = ++time;
    RelSet neighbors = adjacency[u] & set;
    while (neighbors != 0) {
      const std::size_t v = detail::Ctz(neighbors);
      neighbors &= neighbors - 1;

      if (disc[v] == -1) {
        edge_stack.emplace_back(u, v);
        self(self, v, u);
        low[u] = std::min(low[u], low[v]);

        if (low[v] >= disc[u]) {
          RelSet block = 0;
          while (!edge_stack.empty()) {
            auto [x, y] = edge_stack.back();
            edge_stack.pop_back();
            block |= (RelSet{1} << x) | (RelSet{1} << y);
            if ((x == u && y == v) || (x == v && y == u)) break;
          }
          if (block != 0) {
            blocks.push_back(block);
          }
        }
      } else if (v != parent && disc[v] < disc[u]) {
        edge_stack.emplace_back(u, v);
        low[u] = std::min(low[u], disc[v]);
      }
    }
  };

  RelSet remaining = set;
  while (remaining != 0) {
    const std::size_t u = detail::Ctz(remaining);
    remaining &= remaining - 1;
    if (disc[u] != -1) continue;
    dfs(dfs, u, n);
    edge_stack.clear();
  }

  return blocks;
}

RelSet MPDP::Grow(const JoinGraph &graph, RelSet seeds, RelSet allowed) const {
  RelSet visited = seeds & allowed;
  std::queue<std::size_t> queue;
  RelSet pending = visited;
  while (pending != 0) {
    const std::size_t v = detail::Ctz(pending);
    pending &= pending - 1;
    queue.push(v);
  }

  while (!queue.empty()) {
    const auto current = queue.front();
    queue.pop();
    for (const auto &predicate : graph.Predicates()) {
      const auto l = graph.RelationByAlias(predicate.left.alias).id;
      const auto r = graph.RelationByAlias(predicate.right.alias).id;
      std::size_t next = graph.RelationCount();
      if (l == current) {
        next = r;
      } else if (r == current) {
        next = l;
      } else {
        continue;
      }
      const RelSet next_bit = RelSet{1} << next;
      if ((allowed & next_bit) != 0 && (visited & next_bit) == 0) {
        visited |= next_bit;
        queue.push(next);
      }
    }
  }

  return visited;
}

} // namespace volcano
