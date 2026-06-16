#pragma once

#include "volcano/search_strategy.hpp"

#include <string>
#include <unordered_map>

namespace volcano {

// DeHaan & Tompa (SIGMOD 2007) style top-down partitioning search.
//
// Starts from the full relation set and recursively partitions it into
// two connected subgraphs. Uses memoization on (RelSet, RequiredProperty)
// and predicted-cost branch-and-bound pruning.
//
// The partitioner can be either:
//   - Naive: enumerate all subsets, filter by connectivity (generate-and-test)
//   - Mincut: use minimal cuts to directly generate CP-free partitions (future)

enum class PartitionStrategy {
  Naive,   // enumerate all subsets, filter by connectivity
  Mincut,  // minimal-cut based (DeHaan & Tompa 2007) — future work
};

class TopDownPartitioning : public SearchStrategy {
public:
  explicit TopDownPartitioning(PartitionStrategy partition_strategy = PartitionStrategy::Naive,
                               bool allow_cross_products = false);

  std::string Name() const override;
  SearchResult Search(const JoinGraph &graph,
                      const StatsCatalog &stats,
                      const RequiredProperty &property) override;

private:
  // The recursive heart of top-down search.
  // Returns nullopt if no plan can satisfy the property.
  PlanPtr BestPlan(const JoinGraph &graph,
                   RelSet relset,
                   const RequiredProperty &property,
                   SearchTrace &trace);

  // Enumerate valid CP-free partitions of a relation set.
  // Each partition is (L, R) where L ∪ R = relset, L ∩ R = ∅.
  struct Partition {
    RelSet left;
    RelSet right;
  };
  std::vector<Partition> EnumeratePartitions(const JoinGraph &graph, RelSet relset,
                                             SearchTrace &trace) const;

  // Try all physical join methods for a given partition.
  PlanPtr TryJoinMethods(const JoinGraph &graph,
                         RelSet left_set, RelSet right_set,
                         const RequiredProperty &required_prop,
                         PlanPtr current_best,
                         SearchTrace &trace);

  // Predicted-cost lower bound for branch-and-bound.
  double LowerBound(const JoinGraph &graph, RelSet relset,
                    const RequiredProperty &property) const;

  std::string MakeKey(RelSet relset, const RequiredProperty &property) const;

  PartitionStrategy partition_strategy_;
  bool allow_cross_products_;
  std::unordered_map<std::string, PlanPtr> cache_;
};

} // namespace volcano
