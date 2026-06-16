#include "volcano/optimizer.hpp"

#include <algorithm>
#include <cmath>
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

std::size_t SingleBitIndex(RelSet set) {
  if (set == 0 || (set & (set - 1)) != 0) {
    throw std::runtime_error("expected a singleton relation set");
  }
  std::size_t index = 0;
  while ((set & RelSet{1}) == 0) {
    set >>= 1;
    ++index;
  }
  return index;
}

bool Better(const PlanPtr &candidate, const PlanPtr &best) {
  return candidate && (!best || candidate->cost < best->cost);
}

} // namespace

LogicalExpr LogicalExpr::Get(std::size_t relation_id_p) {
  LogicalExpr expr;
  expr.kind = Kind::Get;
  expr.relation_id = relation_id_p;
  expr.relset = RelSet{1} << relation_id_p;
  return expr;
}

LogicalExpr LogicalExpr::Join(RelSet left, RelSet right) {
  LogicalExpr expr;
  expr.kind = Kind::Join;
  expr.left_set = left;
  expr.right_set = right;
  expr.relset = left | right;
  return expr;
}

std::string LogicalExpr::Key() const {
  std::ostringstream out;
  if (kind == Kind::Get) {
    out << "G:" << relation_id;
  } else {
    out << "J:" << left_set << ":" << right_set;
  }
  return out.str();
}

std::string LogicalExpr::ToString(const JoinGraph &graph) const {
  if (kind == Kind::Get) {
    return "Get(" + graph.RelationById(relation_id).alias + ")";
  }
  return "Join(" + RelSetToString(left_set, graph) + ", " + RelSetToString(right_set, graph) + ")";
}

void MemoStore::Clear() {
  groups_.clear();
  group_by_relset_.clear();
  expression_keys_.clear();
}

bool MemoStore::InsertExpression(const LogicalExpr &expr, TraceCounters &trace, bool count_duplicate) {
  const auto key = expr.Key();
  if (expression_keys_.find(key) != expression_keys_.end()) {
    if (count_duplicate) {
      ++trace.duplicate_expressions;
    }
    trace.group_count = GroupCount();
    trace.expression_count = ExpressionCount();
    return false;
  }

  expression_keys_.insert(key);
  auto &group = EnsureGroup(expr.relset);
  group.expressions.push_back(expr);
  ++trace.inserted_expressions;
  trace.group_count = GroupCount();
  trace.expression_count = ExpressionCount();
  return true;
}

const MemoGroup *MemoStore::FindGroup(RelSet relset) const {
  const auto found = group_by_relset_.find(relset);
  if (found == group_by_relset_.end()) {
    return nullptr;
  }
  return &groups_.at(found->second);
}

const std::vector<MemoGroup> &MemoStore::Groups() const {
  return groups_;
}

std::size_t MemoStore::GroupCount() const {
  return groups_.size();
}

std::size_t MemoStore::ExpressionCount() const {
  std::size_t result = 0;
  for (const auto &group : groups_) {
    result += group.expressions.size();
  }
  return result;
}

MemoGroup &MemoStore::EnsureGroup(RelSet relset) {
  const auto found = group_by_relset_.find(relset);
  if (found != group_by_relset_.end()) {
    return groups_.at(found->second);
  }
  const auto id = groups_.size();
  groups_.push_back(MemoGroup{relset, {}});
  group_by_relset_.emplace(relset, id);
  return groups_.back();
}

Optimizer::Optimizer(JoinGraph graph, OptimizerOptions options) : graph_(std::move(graph)), options_(options) {
}

RelSet Optimizer::BuildMemo() {
  memo_.Clear();
  best_cache_.clear();
  trace_ = TraceCounters{};

  std::deque<LogicalExpr> work;
  SeedInitialExpressions(work);
  ExploreTransformations(work);

  trace_.group_count = memo_.GroupCount();
  trace_.expression_count = memo_.ExpressionCount();
  memo_built_ = true;
  return graph_.FullSet();
}

std::optional<PhysicalPlan> Optimizer::Optimize(RelSet root, const RequiredProperty &property) {
  if (!memo_built_) {
    BuildMemo();
  }
  auto plan = OptimizeGroup(root, property);
  if (!plan.has_value() || !*plan) {
    return std::nullopt;
  }
  trace_.chosen_plan_cost = (*plan)->cost;
  return **plan;
}

const JoinGraph &Optimizer::Graph() const {
  return graph_;
}

const MemoStore &Optimizer::Memo() const {
  return memo_;
}

TraceCounters Optimizer::Trace() const {
  auto trace = trace_;
  trace.group_count = memo_.GroupCount();
  trace.expression_count = memo_.ExpressionCount();
  return trace;
}

bool Optimizer::TryInsertJoin(RelSet left, RelSet right, bool from_rule, std::deque<LogicalExpr> &work) {
  if (from_rule) {
    ++trace_.rule_attempts;
  }
  if (!graph_.IsValidJoinSplit(left, right, options_.allow_cross_products)) {
    return false;
  }
  auto expr = LogicalExpr::Join(left, right);
  const auto inserted = memo_.InsertExpression(expr, trace_, from_rule);
  if (inserted) {
    work.push_back(expr);
  }
  return inserted;
}

void Optimizer::SeedInitialExpressions(std::deque<LogicalExpr> &work) {
  for (const auto &relation : graph_.Relations()) {
    const auto expr = LogicalExpr::Get(relation.id);
    if (memo_.InsertExpression(expr, trace_)) {
      work.push_back(expr);
    }
  }

  const auto full = graph_.FullSet();
  if (full == 0) {
    return;
  }
  for (RelSet set = 1; set != 0; ++set) {
    if ((set & ~full) != 0 || PopCount(set) < 2) {
      if (set == full) {
        break;
      }
      continue;
    }
    for (const auto left : graph_.ValidOrderedPartitions(set, options_.allow_cross_products)) {
      const auto right = set ^ left;
      TryInsertJoin(left, right, false, work);
    }
    if (set == full) {
      break;
    }
  }
}

void Optimizer::ExploreTransformations(std::deque<LogicalExpr> &work) {
  while (!work.empty()) {
    const auto expr = work.front();
    work.pop_front();
    ApplyRules(expr, work);
  }
}

void Optimizer::ApplyRules(const LogicalExpr &expr, std::deque<LogicalExpr> &work) {
  if (expr.kind != LogicalExpr::Kind::Join) {
    return;
  }

  TryInsertJoin(expr.right_set, expr.left_set, true, work);

  if (const auto *left_group = memo_.FindGroup(expr.left_set)) {
    for (const auto &left_expr : left_group->expressions) {
      if (left_expr.kind != LogicalExpr::Kind::Join) {
        continue;
      }
      TryInsertJoin(left_expr.left_set, left_expr.right_set | expr.right_set, true, work);
      TryInsertJoin(left_expr.right_set, left_expr.left_set | expr.right_set, true, work);
    }
  }

  if (const auto *right_group = memo_.FindGroup(expr.right_set)) {
    for (const auto &right_expr : right_group->expressions) {
      if (right_expr.kind != LogicalExpr::Kind::Join) {
        continue;
      }
      TryInsertJoin(expr.left_set | right_expr.left_set, right_expr.right_set, true, work);
      TryInsertJoin(expr.left_set | right_expr.right_set, right_expr.left_set, true, work);
    }
  }
}

std::optional<PlanPtr> Optimizer::OptimizeGroup(RelSet relset, const RequiredProperty &property) {
  const auto key = CacheKey(relset, property);
  const auto cached = best_cache_.find(key);
  if (cached != best_cache_.end()) {
    ++trace_.property_cache_hits;
    return cached->second;
  }

  std::optional<PlanPtr> result;
  if (property.IsAny()) {
    result = OptimizeAny(relset);
  } else {
    result = OptimizeSorted(relset, property.sort_key);
  }
  best_cache_[key] = result;
  return result;
}

std::optional<PlanPtr> Optimizer::OptimizeAny(RelSet relset) {
  const auto *group = memo_.FindGroup(relset);
  if (!group) {
    return std::nullopt;
  }

  PlanPtr best;
  for (const auto &expr : group->expressions) {
    auto candidate = OptimizeAnyExpr(expr);
    if (candidate.has_value() && Better(*candidate, best)) {
      best = *candidate;
    }
  }
  if (!best) {
    return std::nullopt;
  }
  return best;
}

std::optional<PlanPtr> Optimizer::OptimizeSorted(RelSet relset, const ColumnRef &sort_key) {
  const auto *group = memo_.FindGroup(relset);
  if (!group) {
    return std::nullopt;
  }

  PlanPtr best;
  for (const auto &expr : group->expressions) {
    auto candidate = OptimizeSortedExpr(expr, sort_key);
    if (candidate.has_value() && Better(*candidate, best)) {
      best = *candidate;
    }
  }

  auto any = OptimizeGroup(relset, RequiredProperty::Any());
  if (any.has_value() && *any) {
    auto sorted = MakeSort(*any, sort_key);
    if (Better(sorted, best)) {
      best = sorted;
    }
  }

  if (!best) {
    return std::nullopt;
  }
  return best;
}

std::optional<PlanPtr> Optimizer::OptimizeAnyExpr(const LogicalExpr &expr) {
  if (expr.kind == LogicalExpr::Kind::Get) {
    return MakeScan(graph_.RelationById(expr.relation_id));
  }

  auto left = OptimizeGroup(expr.left_set, RequiredProperty::Any());
  auto right = OptimizeGroup(expr.right_set, RequiredProperty::Any());
  if (!left.has_value() || !right.has_value() || !*left || !*right) {
    return std::nullopt;
  }

  const auto predicates = graph_.CrossingPredicates(expr.left_set, expr.right_set);
  const auto *predicate = predicates.empty() ? nullptr : &predicates.front();
  PlanPtr best;
  auto hash_join = MakeJoin(PhysicalOp::HashJoin, *left, *right, predicate, RequiredProperty::Any());
  if (Better(hash_join, best)) {
    best = hash_join;
  }
  auto nested_loop = MakeJoin(PhysicalOp::NestedLoopJoin, *left, *right, predicate, RequiredProperty::Any());
  if (Better(nested_loop, best)) {
    best = nested_loop;
  }

  if (predicate != nullptr) {
    const auto left_has_pred_left = (expr.left_set & graph_.MaskForAlias(predicate->left.alias)) != 0;
    const auto left_key = left_has_pred_left ? predicate->left : predicate->right;
    const auto right_key = left_has_pred_left ? predicate->right : predicate->left;
    auto sorted_left = OptimizeGroup(expr.left_set, RequiredProperty::Sorted(left_key));
    auto sorted_right = OptimizeGroup(expr.right_set, RequiredProperty::Sorted(right_key));
    if (sorted_left.has_value() && sorted_right.has_value() && *sorted_left && *sorted_right) {
      auto merge_join = MakeJoin(PhysicalOp::MergeJoin, *sorted_left, *sorted_right, predicate, RequiredProperty::Any());
      if (Better(merge_join, best)) {
        best = merge_join;
      }
    }
  }

  if (!best) {
    return std::nullopt;
  }
  return best;
}

std::optional<PlanPtr> Optimizer::OptimizeSortedExpr(const LogicalExpr &expr, const ColumnRef &sort_key) {
  if (expr.kind == LogicalExpr::Kind::Get) {
    if (graph_.RelationById(expr.relation_id).alias != sort_key.alias) {
      return std::nullopt;
    }
    auto scan = MakeScan(graph_.RelationById(expr.relation_id));
    return MakeSort(scan, sort_key);
  }

  const auto predicates = graph_.CrossingPredicates(expr.left_set, expr.right_set);
  PlanPtr best;
  for (const auto &predicate : predicates) {
    const bool sort_on_left = predicate.left == sort_key;
    const bool sort_on_right = predicate.right == sort_key;
    if (!sort_on_left && !sort_on_right) {
      continue;
    }
    const bool key_from_left_input = (expr.left_set & graph_.MaskForAlias(sort_key.alias)) != 0;
    const auto other_key = sort_on_left ? predicate.right : predicate.left;
    auto sorted_left = OptimizeGroup(expr.left_set, RequiredProperty::Sorted(key_from_left_input ? sort_key : other_key));
    auto sorted_right = OptimizeGroup(expr.right_set, RequiredProperty::Sorted(key_from_left_input ? other_key : sort_key));
    if (sorted_left.has_value() && sorted_right.has_value() && *sorted_left && *sorted_right) {
      auto merge_join =
          MakeJoin(PhysicalOp::MergeJoin, *sorted_left, *sorted_right, &predicate, RequiredProperty::Sorted(sort_key));
      if (Better(merge_join, best)) {
        best = merge_join;
      }
    }
  }

  if (!best) {
    return std::nullopt;
  }
  return best;
}

PlanPtr Optimizer::MakeScan(const Relation &relation) const {
  auto plan = std::make_shared<PhysicalPlan>();
  plan->op = PhysicalOp::SeqScan;
  plan->relset = RelSet{1} << relation.id;
  plan->rows = relation.rows;
  plan->cost = relation.scan_cost;
  plan->property = RequiredProperty::Any();
  plan->relation_alias = relation.alias;
  return plan;
}

PlanPtr Optimizer::MakeSort(PlanPtr child, const ColumnRef &sort_key) const {
  auto plan = std::make_shared<PhysicalPlan>();
  plan->op = PhysicalOp::SortEnforcer;
  plan->relset = child->relset;
  plan->rows = child->rows;
  plan->cost = child->cost + child->rows * std::log2(child->rows + 1.0);
  plan->property = RequiredProperty::Sorted(sort_key);
  plan->child = std::move(child);
  return plan;
}

PlanPtr Optimizer::MakeJoin(PhysicalOp op, PlanPtr left, PlanPtr right, const JoinPredicate *predicate,
                            const RequiredProperty &property) const {
  auto plan = std::make_shared<PhysicalPlan>();
  plan->op = op;
  plan->relset = left->relset | right->relset;
  plan->rows = left->rows * right->rows * graph_.CrossingSelectivity(left->relset, right->relset);
  plan->property = property;
  plan->left = std::move(left);
  plan->right = std::move(right);
  if (predicate != nullptr) {
    plan->predicate = *predicate;
  }

  switch (op) {
  case PhysicalOp::HashJoin:
    plan->cost = plan->left->cost + plan->right->cost + plan->left->rows + plan->right->rows + plan->rows;
    break;
  case PhysicalOp::NestedLoopJoin:
    plan->cost = plan->left->cost + plan->right->cost + plan->left->rows * plan->right->rows;
    break;
  case PhysicalOp::MergeJoin:
    plan->cost = plan->left->cost + plan->right->cost + plan->left->rows + plan->right->rows;
    break;
  default:
    throw std::runtime_error("MakeJoin called with non-join physical operator");
  }
  return plan;
}

std::string Optimizer::CacheKey(RelSet relset, const RequiredProperty &property) const {
  std::ostringstream out;
  out << relset << "|" << property.ToString();
  return out.str();
}

} // namespace volcano
