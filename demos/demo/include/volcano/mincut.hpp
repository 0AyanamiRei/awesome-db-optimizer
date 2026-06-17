#pragma once

#include "volcano/types.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace volcano {

// DeHaan & Tompa (SIGMOD 2007) MincutLazy CP-free partition enumeration.
//
// Core insight: a partition (L,R) of a connected join graph is CP-free iff
// both L and R are connected. Instead of enumerating all 2^(n-1) subsets
// and filtering by connectivity (Naive), MincutLazy:
//
//   1. Builds the biconnection tree (block-cut tree) via Tarjan's algorithm.
//   2. Enumerates only *connected* subsets via BFS expansion.
//   3. For each connected subset, checks if the complement is also connected.
//
// On sparse graphs (chains, trees), the number of connected subsets is O(n²)
// vs O(2^n) all subsets — a dramatic reduction. On dense graphs (cliques)
// the asymptotic is the same, but we still save the connectivity check on
// one side (the enumerated subset is guaranteed connected).
//
// The BC-tree data is available for future optimizations (e.g., pruning
// subsets whose complement is guaranteed disconnected by articulation-
// point analysis).
class MincutPartitioner {
public:
  // Preprocess the join graph: build adjacency bitsets, find bicomponents
  // and articulation points via Tarjan's algorithm, construct BC-tree.
  explicit MincutPartitioner(const JoinGraph &graph);

  // Enumerate all CP-free partitions (L,R) of a connected relation set.
  // Each (L,R) satisfies:
  //   L ∪ R = relset,  L ∩ R = ∅,  both connected,  crossing predicate exists.
  // Returns pairs where L < R (bitmask comparison) to avoid symmetric duplicates.
  std::vector<std::pair<RelSet, RelSet>> EnumeratePartitions(RelSet relset) const;

  // --- Introspection (for debugging / tracing) ---
  std::size_t BicomponentCount() const { return bicomponents_.size(); }
  std::size_t ArticulationPointCount() const;

private:
  // ---- Phase 1: BC-tree construction ----

  // Build adjacency_[i] = bitmask of relations joined with relation i.
  void BuildAdjacency(const JoinGraph &graph);

  // Tarjan's algorithm: DFS over the full join graph to find articulation
  // points and biconnected components (bicomponents), then wire the BC-tree.
  void BuildBCTree();

  // Recursive DFS for Tarjan's algorithm.
  // Pushes edges onto `edge_stack`; pops a bicomponent when low[v] >= disc[u].
  void TarjanDFS(std::size_t u, std::size_t parent,
                 std::vector<int> &disc, std::vector<int> &low, int &time,
                 std::vector<std::pair<std::size_t, std::size_t>> &edge_stack);

  // ---- Phase 2: Partition enumeration ----

  // Enumerate all non-empty proper connected subsets of `relset`.
  // Uses BFS expansion with a visited set — each connected subset is
  // emitted exactly once.
  std::vector<RelSet> EnumerateConnectedSubsets(RelSet relset) const;

  // Fast connectivity test using precomputed adjacency bitsets + BFS.
  bool IsConnectedFast(RelSet set) const;

  // ---- Data ----

  const JoinGraph &graph_;
  std::size_t num_relations_ = 0;

  // Adjacency bitsets for the full join graph.
  // adjacency_[i] has bit j set iff there is a predicate between relation i and j.
  std::vector<RelSet> adjacency_;

  // ---- BC-tree ----
  struct Bicomponent {
    RelSet vertices = 0;           // relations belonging to this bicomponent
    RelSet articulation_pts = 0;   // articulation points within this bicomponent
    std::vector<std::size_t> neighbors; // adjacent bicomponents in BC-tree
  };
  std::vector<Bicomponent> bicomponents_;
  RelSet all_articulation_pts_ = 0;

  // vertex → indices of bicomponents that contain it
  std::vector<std::vector<std::size_t>> vertex_to_bicomponents_;
};

} // namespace volcano
