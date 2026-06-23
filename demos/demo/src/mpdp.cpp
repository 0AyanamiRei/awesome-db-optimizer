#include "volcano/mpdp.hpp"
#include "volcano/search_helpers.hpp"

#include <algorithm>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace volcano {
namespace {

using MemoTable = std::unordered_map<RelSet, PlanPtr>;

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

struct StateCell {
  PlanPtr best;
  SearchTrace trace;
};

void MergeTrace(SearchTrace &target, const SearchTrace &source) {
  target.partitions_explored += source.partitions_explored;
  target.partitions_rejected += source.partitions_rejected;
  target.plans_costed += source.plans_costed;
  target.cache_hits += source.cache_hits;
  target.duplicates_generated += source.duplicates_generated;
  target.rule_applications += source.rule_applications;
  target.branches_pruned += source.branches_pruned;
}

RelSet LowestSubset(RelSet set) {
  return set & (~set + 1);
}

RelSet NextSubset(RelSet subset, RelSet set) {
  return (subset - set) & set;
}

void BuildPlansForPairIntoCell(const MemoTable &memo,
                               StateCell &cell,
                               const JoinGraph &graph,
                               const StatsCatalog &stats,
                               RelSet S_left,
                               RelSet S_right) {
  const auto predicates = graph.CrossingPredicates(S_left, S_right);
  const auto *predicate = predicates.empty() ? nullptr : &predicates.front();

  const auto left_plan = memo.find(S_left);
  const auto right_plan = memo.find(S_right);
  if (left_plan != memo.end() && left_plan->second &&
      right_plan != memo.end() && right_plan->second) {
    auto hash_join = MakeJoin(PhysicalOp::HashJoin,
                              left_plan->second, right_plan->second,
                              graph, stats, predicate, RequiredProperty::Any());
    ++cell.trace.plans_costed;
    detail::TryUpdate(cell.best, hash_join);

    auto nested_loop = MakeJoin(PhysicalOp::NestedLoopJoin,
                                left_plan->second, right_plan->second,
                                graph, stats, predicate, RequiredProperty::Any());
    ++cell.trace.plans_costed;
    detail::TryUpdate(cell.best, nested_loop);
  }
}

void MergeCell(MemoTable &memo, RelSet set, const StateCell &cell) {
  if (cell.best) {
    auto &entry = memo[set];
    detail::TryUpdate(entry, cell.best);
  }
}

} // namespace

MPDP::MPDP(std::size_t thread_count)
    : thread_count_(std::max<std::size_t>(1, thread_count)) {}

std::string MPDP::Name() const {
  return thread_count_ > 1 ? "MPDP(Parallel)" : "MPDP";
}

SearchResult MPDP::Search(const JoinGraph &graph,
                          const StatsCatalog &stats,
                          const RequiredProperty &property) {
  SearchTrace trace;
  MemoTable memo;

  const auto full = graph.FullSet();
  if (full == 0) {
    SearchResult result;
    result.trace = trace;
    return result;
  }

  for (const auto &relation : graph.Relations()) {
    const RelSet singleton = RelSet{1} << relation.id;
    memo[singleton] = MakeScan(relation, stats);
    ++trace.plans_costed;
    trace.dp_cells_filled = memo.size();
  }

  const auto connected_by_size = detail::CollectConnectedSubsets(graph);
  const std::size_t n = graph.RelationCount();

  for (std::size_t i = 2; i <= n; ++i) {
    const auto &Si = connected_by_size[i];

    std::vector<StateCell> cells(Si.size());

    auto fill_cell = [&](std::size_t index) {
      const RelSet S = Si[index];
      const auto join_pairs = EnumerateJoinPairs(graph, S);
      for (const auto &[S_left, S_right] : join_pairs) {
        ++cells[index].trace.partitions_explored;
        BuildPlansForPairIntoCell(memo, cells[index], graph, stats, S_left, S_right);
      }
    };

    if (thread_count_ <= 1 || Si.size() <= 1) {
      for (std::size_t index = 0; index < Si.size(); ++index) {
        fill_cell(index);
      }
    } else {
      std::size_t next_index = 0;
      std::mutex mutex;
      const std::size_t worker_count = std::min(thread_count_, Si.size());
      std::vector<std::thread> workers;
      workers.reserve(worker_count);

      for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&]() {
          while (true) {
            std::size_t index = 0;
            {
              std::lock_guard<std::mutex> lock(mutex);
              if (next_index >= Si.size()) {
                return;
              }
              index = next_index++;
            }
            fill_cell(index);
          }
        });
      }

      for (auto &worker : workers) {
        worker.join();
      }
    }

    for (std::size_t index = 0; index < cells.size(); ++index) {
      MergeCell(memo, Si[index], cells[index]);
      const auto had_plan = cells[index].best != nullptr;
      MergeTrace(trace, cells[index].trace);
      if (had_plan) {
        trace.dp_cells_filled = memo.size();
      }
    }
  }

  SearchResult result;
  PlanPtr best;
  auto it = memo.find(full);
  if (it != memo.end() && it->second) {
    best = it->second;
  }

  if (!property.IsAny() && best) {
    best = MakeSort(best, property.sort_key);
    ++trace.plans_costed;
  }

  if (best) {
    result.has_plan = true;
    result.best_plan = *best;
    trace.best_cost = result.best_plan.cost;
  }
  trace.plans_cached = memo.size();
  result.trace = trace;
  return result;
}

std::vector<MPDP::JoinPair> MPDP::EnumerateJoinPairs(const JoinGraph &graph,
                                                     RelSet set) const {
  std::vector<JoinPair> result;
  std::unordered_set<JoinPairKey, JoinPairKeyHash> emitted;

  for (RelSet block : FindBlocks(graph, set)) {
    for (RelSet lb = LowestSubset(block); lb != block; lb = NextSubset(lb, block)) {
      const RelSet rb = block ^ lb;
      if (!graph.IsConnected(lb) || !graph.IsConnected(rb)) continue;
      if (!graph.HasPredicateAcross(lb, rb)) continue;

      const RelSet left = Grow(graph, lb, set & ~rb);
      const RelSet right = Grow(graph, rb, set & ~lb);
      if ((left | right) != set || (left & right) != 0) continue;
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
