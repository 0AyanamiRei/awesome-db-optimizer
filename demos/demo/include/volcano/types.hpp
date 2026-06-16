#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace volcano {

using RelSet = std::uint64_t;
using GroupId = std::size_t;

struct ColumnRef {
  std::string alias;
  std::string column;

  std::string ToString() const;
  bool operator==(const ColumnRef &other) const;
  bool operator!=(const ColumnRef &other) const;
};

struct Relation {
  std::size_t id = 0;
  std::string alias;
  std::string table_name;
  double rows = 0.0;
  double scan_cost = 0.0;
};

struct JoinPredicate {
  ColumnRef left;
  ColumnRef right;
  double selectivity = 1.0;

  std::string ToString() const;
};

class JoinGraph {
public:
  std::size_t AddRelation(const std::string &alias, const std::string &table_name, double rows, double scan_cost);
  void AddPredicate(ColumnRef left, ColumnRef right, double selectivity);

  std::size_t RelationCount() const;
  std::size_t PredicateCount() const;
  const Relation &RelationByAlias(const std::string &alias) const;
  const Relation &RelationById(std::size_t id) const;
  const std::vector<Relation> &Relations() const;
  const std::vector<JoinPredicate> &Predicates() const;

  RelSet FullSet() const;
  RelSet MaskForAlias(const std::string &alias) const;
  bool ContainsAlias(const std::string &alias) const;
  bool HasPredicateAcross(RelSet left, RelSet right) const;
  double CrossingSelectivity(RelSet left, RelSet right) const;
  std::vector<JoinPredicate> CrossingPredicates(RelSet left, RelSet right) const;
  bool IsConnected(RelSet set) const;
  bool IsValidJoinSplit(RelSet left, RelSet right, bool allow_cross_products) const;
  std::vector<RelSet> ValidOrderedPartitions(RelSet set, bool allow_cross_products) const;

private:
  std::vector<Relation> relations_;
  std::vector<JoinPredicate> predicates_;
  std::unordered_map<std::string, std::size_t> alias_to_index_;
};

enum class PropertyKind { Any, Sorted };

struct RequiredProperty {
  PropertyKind kind = PropertyKind::Any;
  ColumnRef sort_key;

  static RequiredProperty Any();
  static RequiredProperty Sorted(ColumnRef key);

  bool IsAny() const;
  std::string ToString() const;
  bool operator==(const RequiredProperty &other) const;
  bool operator!=(const RequiredProperty &other) const;
};

enum class PhysicalOp { SeqScan, HashJoin, NestedLoopJoin, MergeJoin, SortEnforcer };

struct PhysicalPlan;
using PlanPtr = std::shared_ptr<PhysicalPlan>;

struct PhysicalPlan {
  PhysicalOp op = PhysicalOp::SeqScan;
  RelSet relset = 0;
  double rows = 0.0;
  double cost = 0.0;
  RequiredProperty property = RequiredProperty::Any();
  std::string relation_alias;
  std::optional<JoinPredicate> predicate;
  PlanPtr left;
  PlanPtr right;
  PlanPtr child;

  std::string ToString() const;
};

struct TraceCounters {
  std::uint64_t rule_attempts = 0;
  std::uint64_t inserted_expressions = 0;
  std::uint64_t duplicate_expressions = 0;
  std::uint64_t group_count = 0;
  std::uint64_t expression_count = 0;
  std::uint64_t property_cache_hits = 0;
  double chosen_plan_cost = 0.0;
};

std::string RelSetToString(RelSet set, const JoinGraph &graph);
std::string PhysicalOpName(PhysicalOp op);

} // namespace volcano
