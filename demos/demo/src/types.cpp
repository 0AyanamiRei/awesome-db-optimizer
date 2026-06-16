#include "volcano/types.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace volcano {

std::string ColumnRef::ToString() const {
  return alias + "." + column;
}

bool ColumnRef::operator==(const ColumnRef &other) const {
  return alias == other.alias && column == other.column;
}

bool ColumnRef::operator!=(const ColumnRef &other) const {
  return !(*this == other);
}

std::string JoinPredicate::ToString() const {
  std::ostringstream out;
  out << left.ToString() << " = " << right.ToString() << " (sel=" << selectivity << ")";
  return out.str();
}

std::size_t JoinGraph::AddRelation(const std::string &alias, const std::string &table_name, double rows,
                                   double scan_cost) {
  if (relations_.size() >= 63) {
    throw std::runtime_error("V1 RelSet supports at most 63 relations");
  }
  if (alias.empty()) {
    throw std::runtime_error("relation alias must not be empty");
  }
  if (alias_to_index_.find(alias) != alias_to_index_.end()) {
    throw std::runtime_error("duplicate relation alias: " + alias);
  }
  const auto id = relations_.size();
  relations_.push_back(Relation{id, alias, table_name, rows, scan_cost});
  alias_to_index_.emplace(alias, id);
  return id;
}

void JoinGraph::AddPredicate(ColumnRef left, ColumnRef right, double selectivity) {
  if (!ContainsAlias(left.alias)) {
    throw std::runtime_error("join predicate references unknown alias: " + left.alias);
  }
  if (!ContainsAlias(right.alias)) {
    throw std::runtime_error("join predicate references unknown alias: " + right.alias);
  }
  if (left.alias == right.alias) {
    throw std::runtime_error("join predicate must reference two different aliases: " + left.ToString());
  }
  if (selectivity <= 0.0) {
    throw std::runtime_error("join selectivity must be positive");
  }
  predicates_.push_back(JoinPredicate{std::move(left), std::move(right), selectivity});
}

std::size_t JoinGraph::RelationCount() const {
  return relations_.size();
}

std::size_t JoinGraph::PredicateCount() const {
  return predicates_.size();
}

const Relation &JoinGraph::RelationByAlias(const std::string &alias) const {
  const auto found = alias_to_index_.find(alias);
  if (found == alias_to_index_.end()) {
    throw std::runtime_error("unknown relation alias: " + alias);
  }
  return relations_.at(found->second);
}

const Relation &JoinGraph::RelationById(std::size_t id) const {
  if (id >= relations_.size()) {
    throw std::runtime_error("relation id out of range");
  }
  return relations_[id];
}

const std::vector<Relation> &JoinGraph::Relations() const {
  return relations_;
}

const std::vector<JoinPredicate> &JoinGraph::Predicates() const {
  return predicates_;
}

RelSet JoinGraph::FullSet() const {
  if (relations_.empty()) {
    return 0;
  }
  return (RelSet{1} << relations_.size()) - 1;
}

RelSet JoinGraph::MaskForAlias(const std::string &alias) const {
  return RelSet{1} << RelationByAlias(alias).id;
}

bool JoinGraph::ContainsAlias(const std::string &alias) const {
  return alias_to_index_.find(alias) != alias_to_index_.end();
}

bool JoinGraph::HasPredicateAcross(RelSet left, RelSet right) const {
  return !CrossingPredicates(left, right).empty();
}

double JoinGraph::CrossingSelectivity(RelSet left, RelSet right) const {
  double result = 1.0;
  for (const auto &predicate : CrossingPredicates(left, right)) {
    result *= predicate.selectivity;
  }
  return result;
}

std::vector<JoinPredicate> JoinGraph::CrossingPredicates(RelSet left, RelSet right) const {
  std::vector<JoinPredicate> result;
  for (const auto &predicate : predicates_) {
    const auto left_mask = MaskForAlias(predicate.left.alias);
    const auto right_mask = MaskForAlias(predicate.right.alias);
    const bool left_to_right = (left & left_mask) != 0 && (right & right_mask) != 0;
    const bool right_to_left = (left & right_mask) != 0 && (right & left_mask) != 0;
    if (left_to_right || right_to_left) {
      result.push_back(predicate);
    }
  }
  return result;
}

bool JoinGraph::IsConnected(RelSet set) const {
  if (set == 0) {
    return false;
  }
  if ((set & (set - 1)) == 0) {
    return true;
  }

  std::queue<std::size_t> queue;
  RelSet visited = 0;
  for (std::size_t i = 0; i < relations_.size(); ++i) {
    if ((set & (RelSet{1} << i)) != 0) {
      queue.push(i);
      visited |= RelSet{1} << i;
      break;
    }
  }

  while (!queue.empty()) {
    const auto current = queue.front();
    queue.pop();
    for (const auto &predicate : predicates_) {
      const auto l = RelationByAlias(predicate.left.alias).id;
      const auto r = RelationByAlias(predicate.right.alias).id;
      std::optional<std::size_t> next;
      if (l == current) {
        next = r;
      } else if (r == current) {
        next = l;
      }
      if (!next.has_value()) {
        continue;
      }
      const RelSet next_bit = RelSet{1} << *next;
      if ((set & next_bit) != 0 && (visited & next_bit) == 0) {
        visited |= next_bit;
        queue.push(*next);
      }
    }
  }
  return (visited & set) == set;
}

bool JoinGraph::IsValidJoinSplit(RelSet left, RelSet right, bool allow_cross_products) const {
  if (left == 0 || right == 0 || (left & right) != 0) {
    return false;
  }
  if (allow_cross_products) {
    return true;
  }
  return IsConnected(left) && IsConnected(right) && HasPredicateAcross(left, right);
}

std::vector<RelSet> JoinGraph::ValidOrderedPartitions(RelSet set, bool allow_cross_products) const {
  std::vector<RelSet> result;
  for (RelSet left = (set - 1) & set; left != 0; left = (left - 1) & set) {
    const RelSet right = set ^ left;
    if (IsValidJoinSplit(left, right, allow_cross_products)) {
      result.push_back(left);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

RequiredProperty RequiredProperty::Any() {
  return RequiredProperty{};
}

RequiredProperty RequiredProperty::Sorted(ColumnRef key) {
  RequiredProperty result;
  result.kind = PropertyKind::Sorted;
  result.sort_key = std::move(key);
  return result;
}

bool RequiredProperty::IsAny() const {
  return kind == PropertyKind::Any;
}

std::string RequiredProperty::ToString() const {
  if (kind == PropertyKind::Any) {
    return "Any";
  }
  return "Sorted(" + sort_key.ToString() + ")";
}

bool RequiredProperty::operator==(const RequiredProperty &other) const {
  return kind == other.kind && (kind == PropertyKind::Any || sort_key == other.sort_key);
}

bool RequiredProperty::operator!=(const RequiredProperty &other) const {
  return !(*this == other);
}

std::string PhysicalPlan::ToString() const {
  switch (op) {
  case PhysicalOp::SeqScan:
    return "SeqScan(" + relation_alias + ")";
  case PhysicalOp::HashJoin:
    return "HashJoin(" + left->ToString() + ", " + right->ToString() + ")";
  case PhysicalOp::NestedLoopJoin:
    return "NestedLoopJoin(" + left->ToString() + ", " + right->ToString() + ")";
  case PhysicalOp::MergeJoin:
    return "MergeJoin(" + left->ToString() + ", " + right->ToString() + ")";
  case PhysicalOp::SortEnforcer:
    return "Sort[" + property.sort_key.ToString() + "](" + child->ToString() + ")";
  }
  return "UnknownPlan";
}

std::string RelSetToString(RelSet set, const JoinGraph &graph) {
  std::ostringstream out;
  out << "{";
  bool first = true;
  for (const auto &relation : graph.Relations()) {
    const auto bit = RelSet{1} << relation.id;
    if ((set & bit) == 0) {
      continue;
    }
    if (!first) {
      out << ",";
    }
    first = false;
    out << relation.alias;
  }
  out << "}";
  return out.str();
}

std::string PhysicalOpName(PhysicalOp op) {
  switch (op) {
  case PhysicalOp::SeqScan:
    return "SeqScan";
  case PhysicalOp::HashJoin:
    return "HashJoin";
  case PhysicalOp::NestedLoopJoin:
    return "NestedLoopJoin";
  case PhysicalOp::MergeJoin:
    return "MergeJoin";
  case PhysicalOp::SortEnforcer:
    return "SortEnforcer";
  }
  return "Unknown";
}

} // namespace volcano
