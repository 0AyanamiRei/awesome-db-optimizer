#include "volcano/mincut.hpp"
#include "volcano/search_helpers.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace volcano {

namespace {

// Isolate the lowest set bit.
RelSet LowBit(RelSet set) {
  return set & (~set + 1);
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

MincutPartitioner::MincutPartitioner(const JoinGraph &graph)
    : graph_(graph)
    , num_relations_(graph.RelationCount()) {
  if (num_relations_ > 0) {
    BuildAdjacency(graph);
    BuildBCTree();
  }
}

void MincutPartitioner::BuildAdjacency(const JoinGraph &graph) {
  adjacency_.assign(num_relations_, 0);
  for (const auto &pred : graph.Predicates()) {
    const auto l = graph.RelationByAlias(pred.left.alias).id;
    const auto r = graph.RelationByAlias(pred.right.alias).id;
    adjacency_[l] |= RelSet{1} << r;
    adjacency_[r] |= RelSet{1} << l;
  }
}

// ============================================================================
// Tarjan's algorithm for biconnected components
// ============================================================================

void MincutPartitioner::BuildBCTree() {
  bicomponents_.clear();
  all_articulation_pts_ = 0;
  vertex_to_bicomponents_.assign(num_relations_, {});

  std::vector<int> disc(num_relations_, -1);
  std::vector<int> low(num_relations_, -1);
  int time = 0;
  std::vector<std::pair<std::size_t, std::size_t>> edge_stack;

  // The join graph may have multiple connected components.
  // Run Tarjan DFS on each unvisited vertex.
  for (std::size_t u = 0; u < num_relations_; ++u) {
    if (disc[u] == -1) {
      TarjanDFS(u, num_relations_, disc, low, time, edge_stack);

      // After finishing a connected component, any remaining edges on the
      // stack form the last bicomponent of that component.
      if (!edge_stack.empty()) {
        Bicomponent bc;
        while (!edge_stack.empty()) {
          auto [x, y] = edge_stack.back();
          edge_stack.pop_back();
          bc.vertices |= (RelSet{1} << x) | (RelSet{1} << y);
        }
        bicomponents_.push_back(std::move(bc));
      }
      // If the component is a single isolated vertex with no edges,
      // it forms its own (degenerate) bicomponent.
      else if ((adjacency_[u] & graph_.FullSet()) == 0) {
        Bicomponent bc;
        bc.vertices = RelSet{1} << u;
        bicomponents_.push_back(std::move(bc));
      }
    }
  }

  // ---- Build BC-tree edges ----
  // Two bicomponents are adjacent iff they share at least one vertex.
  // (Shared vertices are exactly the articulation points.)
  for (std::size_t i = 0; i < bicomponents_.size(); ++i) {
    for (std::size_t j = i + 1; j < bicomponents_.size(); ++j) {
      if ((bicomponents_[i].vertices & bicomponents_[j].vertices) != 0) {
        bicomponents_[i].neighbors.push_back(j);
        bicomponents_[j].neighbors.push_back(i);
      }
    }
  }

  // ---- Populate vertex → bicomponents map ----
  for (std::size_t i = 0; i < bicomponents_.size(); ++i) {
    RelSet v = bicomponents_[i].vertices;
    while (v != 0) {
      std::size_t idx = detail::Ctz(v);
      v &= v - 1;
      vertex_to_bicomponents_[idx].push_back(i);
    }
  }
}

void MincutPartitioner::TarjanDFS(
    std::size_t u, std::size_t parent,
    std::vector<int> &disc, std::vector<int> &low, int &time,
    std::vector<std::pair<std::size_t, std::size_t>> &edge_stack) {

  disc[u] = low[u] = ++time;
  int children = 0;

  RelSet neighbors = adjacency_[u];
  while (neighbors != 0) {
    const std::size_t v = detail::Ctz(neighbors);
    neighbors &= neighbors - 1;

    if (v == parent) continue;

    if (disc[v] == -1) {
      // Tree edge
      edge_stack.emplace_back(u, v);
      ++children;

      TarjanDFS(v, u, disc, low, time, edge_stack);

      low[u] = std::min(low[u], low[v]);

      // Articulation point check:
      // (a) root of DFS with ≥2 children, or
      // (b) non-root with a child whose low-link ≥ disc[u]
      const bool is_ap =
          (parent == num_relations_ && children > 1) ||
          (parent != num_relations_ && low[v] >= disc[u]);

      if (is_ap) {
        all_articulation_pts_ |= RelSet{1} << u;

        // Pop edges until (u,v) to form a bicomponent
        Bicomponent bc;
        bc.articulation_pts |= RelSet{1} << u;
        while (!edge_stack.empty()) {
          auto [x, y] = edge_stack.back();
          edge_stack.pop_back();
          bc.vertices |= (RelSet{1} << x) | (RelSet{1} << y);
          if ((x == u && y == v) || (x == v && y == u)) break;
        }
        bicomponents_.push_back(std::move(bc));
      }
    } else if (disc[v] < disc[u]) {
      // Back edge to ancestor (not parent)
      edge_stack.emplace_back(u, v);
      low[u] = std::min(low[u], disc[v]);
    }
  }
}

std::size_t MincutPartitioner::ArticulationPointCount() const {
  return detail::PopCount(all_articulation_pts_);
}

// ============================================================================
// Partition enumeration
// ============================================================================

std::vector<RelSet> MincutPartitioner::EnumerateConnectedSubsets(RelSet relset) const {
  std::vector<RelSet> result;
  if (relset == 0) return result;

  std::unordered_set<RelSet> visited;

  // BFS expansion: from each connected subset, explore by adding one
  // neighboring vertex that is still in `relset`.  The visited set
  // guarantees each connected subset is emitted exactly once.

  // Seed the BFS from each vertex in relset.
  // For correctness, we only need to seed from one vertex per connected
  // component of the induced subgraph.  Since callers guarantee `relset`
  // is connected, seeding from its lowest-bit vertex is sufficient.
  // We still seed from all vertices to keep the code simple and robust
  // against the (unlikely) case of a disconnected relset.
  std::vector<RelSet> queue;
  {
    RelSet remaining = relset;
    while (remaining != 0) {
      const RelSet seed = LowBit(remaining);
      remaining ^= seed;
      if (visited.insert(seed).second) {
        queue.push_back(seed);
      }
    }
  }

  std::size_t head = 0;
  while (head < queue.size()) {
    const RelSet cur = queue[head++];

    // Emit all non-empty proper subsets (the full relset is not a valid
    // partition side — the other side would be empty).
    if (cur != relset) {
      result.push_back(cur);
    }

    // Compute the frontier: all neighbors of `cur` within `relset` \ `cur`.
    RelSet nbor_union = 0;
    {
      RelSet c = cur;
      while (c != 0) {
        const std::size_t v = detail::Ctz(c);
        c &= c - 1;
        nbor_union |= adjacency_[v];
      }
    }
    const RelSet frontier = nbor_union & relset & ~cur;

    // Expand: add one frontier vertex at a time.
    RelSet cand = frontier;
    while (cand != 0) {
      const RelSet v_bit = LowBit(cand);
      cand ^= v_bit;
      const RelSet next = cur | v_bit;
      if (visited.insert(next).second) {
        queue.push_back(next);
      }
    }
  }

  return result;
}

bool MincutPartitioner::IsConnectedFast(RelSet set) const {
  if (set == 0) return false;
  if ((set & (set - 1)) == 0) return true; // singleton — trivially connected

  // BFS within `set` using precomputed adjacency bitsets.
  const std::size_t start = detail::Ctz(set);
  RelSet visited = RelSet{1} << start;
  RelSet frontier = adjacency_[start] & set & ~visited;

  while (frontier != 0) {
    const RelSet v_bit = LowBit(frontier);
    frontier ^= v_bit;
    const std::size_t v = detail::Ctz(v_bit);
    visited |= v_bit;
    frontier |= adjacency_[v] & set & ~visited;
  }

  return visited == set;
}

std::vector<std::pair<RelSet, RelSet>>
MincutPartitioner::EnumeratePartitions(RelSet relset) const {
  std::vector<std::pair<RelSet, RelSet>> result;
  if (detail::PopCount(relset) <= 1) return result;

  // Step 1: enumerate all non-empty proper connected subsets of `relset`.
  const auto connected_subsets = EnumerateConnectedSubsets(relset);

  // Step 2: for each connected subset C, check whether relset\C is also
  // connected.  If so, (C, relset\C) is a CP-free partition.
  // Emit only when C < relset\C (bitmask ordering) to avoid symmetric
  // duplicates, e.g., both ({a},{b,c}) and ({b,c},{a}).
  for (RelSet c : connected_subsets) {
    const RelSet complement = relset ^ c;
    // Sanity: c must be a proper subset, so complement is non-empty.
    // Both sides must be connected for a CP-free partition.
    if (!IsConnectedFast(complement)) continue;

    // Avoid duplicate: emit only one of (c, complement) and (complement, c).
    if (c < complement) {
      result.emplace_back(c, complement);
    }
  }

  return result;
}

} // namespace volcano
