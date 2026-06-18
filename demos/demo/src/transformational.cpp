#include "volcano/transformational.hpp"
#include "volcano/cost_model.hpp"

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

} // namespace

// --- LogicalExpr ---

LogicalExpr LogicalExpr::Get(std::size_t relation_id) {
  LogicalExpr expr;
  expr.kind = Kind::Get;
  expr.relation_id = relation_id;
  expr.relset = RelSet{1} << relation_id;
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
  return "Join(" + RelSetToString(left_set, graph) + ", " +
         RelSetToString(right_set, graph) + ")";
}

// --- MemoStore ---

void MemoStore::Clear() {
  groups_.clear();
  group_by_relset_.clear();
  expression_keys_.clear();
}

bool MemoStore::InsertExpression(const LogicalExpr &expr, SearchTrace &trace, bool count_duplicate) {
  const auto key = expr.Key();
  if (expression_keys_.find(key) != expression_keys_.end()) {
    if (count_duplicate) {
      ++trace.duplicates_generated;
    }
    return false;
  }

  expression_keys_.insert(key);
  auto &group = EnsureGroup(expr.relset);
  group.expressions.push_back(expr);
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

// --- Transformational ---

SearchResult Transformational::Search(const JoinGraph &graph,
                                       const StatsCatalog &stats,
                                       const RequiredProperty &property) {
  SearchTrace trace;
  memo_.Clear();
  best_cache_.clear();

  const auto full = graph.FullSet();
  if (full == 0) {
    SearchResult result;
    result.trace = trace;
    return result;
  }

  // Phase 1: BuildMemo — bottom-up enumeration + transformation rules
  std::deque<LogicalExpr> work;
  SeedInitialExpressions(graph, false, work, trace);
  ExploreTransformations(graph, false, work, trace);

  // Phase 2: Top-down physical optimization on the memo
  SearchResult result;
  auto plan = OptimizeGroup(graph, stats, full, property, trace);
  if (plan) {
    result.has_plan = true;
    result.best_plan = *plan;
  }
  result.trace = trace;
  result.trace.best_cost = plan ? plan->cost : 0.0;
  result.trace.plans_cached = best_cache_.size();
  return result;
}

bool Transformational::TryInsertJoin(const JoinGraph &graph, bool allow_cp,
                                      RelSet left, RelSet right, bool from_rule,
                                      std::deque<LogicalExpr> &work, SearchTrace &trace) {
  if (from_rule) {
    ++trace.rule_applications;
  }
  if (!graph.IsValidJoinSplit(left, right, allow_cp)) {
    return false;
  }
  auto expr = LogicalExpr::Join(left, right);
  const auto inserted = memo_.InsertExpression(expr, trace, from_rule);
  if (inserted) {
    work.push_back(expr);
  }
  return inserted;
}

void Transformational::SeedInitialExpressions(const JoinGraph &graph, bool allow_cp,
                                               std::deque<LogicalExpr> &work,
                                               SearchTrace &trace) {
  // Singleton Gets
  for (const auto &relation : graph.Relations()) {
    const auto expr = LogicalExpr::Get(relation.id);
    if (memo_.InsertExpression(expr, trace)) {
      work.push_back(expr);
    }
  }

  // All valid partitions of all relation sets
  const auto full = graph.FullSet();
  if (full == 0) return;

  for (RelSet set = 1; set != 0; ++set) {
    if ((set & ~full) != 0 || PopCount(set) < 2) {
      if (set == full) break;
      continue;
    }
    for (const auto left : graph.ValidOrderedPartitions(set, allow_cp)) {
      const auto right = set ^ left;
      TryInsertJoin(graph, allow_cp, left, right, false, work, trace);
    }
    if (set == full) break;
  }
}

void Transformational::ExploreTransformations(const JoinGraph &graph, bool allow_cp,
                                               std::deque<LogicalExpr> &work,
                                               SearchTrace &trace) {
  while (!work.empty()) {
    const auto expr = work.front();
    work.pop_front();
    ApplyRules(graph, allow_cp, expr, work, trace);
  }
}

void Transformational::ApplyRules(const JoinGraph &graph, bool allow_cp,
                                   const LogicalExpr &expr,
                                   std::deque<LogicalExpr> &work,
                                   SearchTrace &trace) {
  if (expr.kind != LogicalExpr::Kind::Join) return;

  // Commutativity: Join(L, R) → Join(R, L)
  TryInsertJoin(graph, allow_cp, expr.right_set, expr.left_set, true, work, trace);

  // Associativity-like rewrites from left child
  if (const auto *left_group = memo_.FindGroup(expr.left_set)) {
    for (const auto &left_expr : left_group->expressions) {
      if (left_expr.kind != LogicalExpr::Kind::Join) continue;
      // (A⋈B)⋈C → A⋈(B⋈C)  and  B⋈(A⋈C)
      TryInsertJoin(graph, allow_cp, left_expr.left_set,
                    left_expr.right_set | expr.right_set, true, work, trace);
      TryInsertJoin(graph, allow_cp, left_expr.right_set,
                    left_expr.left_set | expr.right_set, true, work, trace);
    }
  }

  // Associativity-like rewrites from right child
  if (const auto *right_group = memo_.FindGroup(expr.right_set)) {
    for (const auto &right_expr : right_group->expressions) {
      if (right_expr.kind != LogicalExpr::Kind::Join) continue;
      // A⋈(B⋈C) → (A⋈B)⋈C  and  (A⋈C)⋈B
      TryInsertJoin(graph, allow_cp, expr.left_set | right_expr.left_set,
                    right_expr.right_set, true, work, trace);
      TryInsertJoin(graph, allow_cp, expr.left_set | right_expr.right_set,
                    right_expr.left_set, true, work, trace);
    }
  }
}

// --- Physical Optimization on Memo ---

std::string Transformational::CacheKey(RelSet relset, const RequiredProperty &property) const {
  std::ostringstream out;
  out << relset << "|" << property.ToString();
  return out.str();
}

PlanPtr Transformational::OptimizeGroup(const JoinGraph &graph, const StatsCatalog &stats, RelSet relset,
                                         const RequiredProperty &property,
                                         SearchTrace &trace) {
  const auto key = CacheKey(relset, property);
  const auto cached = best_cache_.find(key);
  if (cached != best_cache_.end()) {
    ++trace.cache_hits;
    return cached->second;
  }

  PlanPtr result;
  if (property.IsAny()) {
    result = OptimizeAny(graph, stats, relset, trace);
  } else {
    result = OptimizeSorted(graph, stats, relset, property.sort_key, trace);
  }
  best_cache_[key] = result;
  return result;
}

PlanPtr Transformational::OptimizeAny(const JoinGraph &graph, const StatsCatalog &stats, RelSet relset,
                                       SearchTrace &trace) {
  const auto *group = memo_.FindGroup(relset);
  if (!group) return nullptr;

  PlanPtr best;
  for (const auto &expr : group->expressions) {
    auto candidate = OptimizeAnyExpr(graph, stats, expr, trace);
    if (Better(candidate, best)) best = candidate;
  }
  return best;
}

PlanPtr Transformational::OptimizeSorted(const JoinGraph &graph, const StatsCatalog &stats, RelSet relset,
                                          const ColumnRef &sort_key,
                                          SearchTrace &trace) {
  const auto *group = memo_.FindGroup(relset);
  if (!group) return nullptr;

  PlanPtr best;
  for (const auto &expr : group->expressions) {
    auto candidate = OptimizeSortedExpr(graph, stats, expr, sort_key, trace);
    if (Better(candidate, best)) best = candidate;
  }

  // SortEnforcer fallback: take best Any plan and add Sort
  auto any = OptimizeGroup(graph, stats, relset, RequiredProperty::Any(), trace);
  if (any) {
    auto sorted = MakeSort(any, sort_key);
    ++trace.plans_costed;
    if (Better(sorted, best)) best = sorted;
  }

  return best;
}

PlanPtr Transformational::OptimizeAnyExpr(const JoinGraph &graph,
                                           const StatsCatalog &stats,
                                           const LogicalExpr &expr,
                                           SearchTrace &trace) {
  if (expr.kind == LogicalExpr::Kind::Get) {
    return MakeScan(graph.RelationById(expr.relation_id), stats);
  }

  auto left = OptimizeGroup(graph, stats, expr.left_set, RequiredProperty::Any(), trace);
  auto right = OptimizeGroup(graph, stats, expr.right_set, RequiredProperty::Any(), trace);
  if (!left || !right) return nullptr;

  const auto predicates = graph.CrossingPredicates(expr.left_set, expr.right_set);
  const auto *predicate = predicates.empty() ? nullptr : &predicates.front();
  PlanPtr best;

  auto hash_join = MakeJoin(PhysicalOp::HashJoin, left, right, graph, stats,
                             predicate, RequiredProperty::Any());
  ++trace.plans_costed;
  if (Better(hash_join, best)) best = hash_join;

  auto nested_loop = MakeJoin(PhysicalOp::NestedLoopJoin, left, right, graph, stats,
                               predicate, RequiredProperty::Any());
  ++trace.plans_costed;
  if (Better(nested_loop, best)) best = nested_loop;

  if (predicate != nullptr) {
    const auto left_has_pred_left = (expr.left_set & graph.MaskForAlias(predicate->left.alias)) != 0;
    const auto left_key = left_has_pred_left ? predicate->left : predicate->right;
    const auto right_key = left_has_pred_left ? predicate->right : predicate->left;
    auto sorted_left = OptimizeGroup(graph, stats, expr.left_set,
                                      RequiredProperty::Sorted(left_key), trace);
    auto sorted_right = OptimizeGroup(graph, stats, expr.right_set,
                                       RequiredProperty::Sorted(right_key), trace);
    if (sorted_left && sorted_right) {
      auto merge_join = MakeJoin(PhysicalOp::MergeJoin, sorted_left, sorted_right,
                                  graph, stats, predicate, RequiredProperty::Any());
      ++trace.plans_costed;
      if (Better(merge_join, best)) best = merge_join;
    }
  }

  return best;
}

PlanPtr Transformational::OptimizeSortedExpr(const JoinGraph &graph,
                                              const StatsCatalog &stats,
                                              const LogicalExpr &expr,
                                              const ColumnRef &sort_key,
                                              SearchTrace &trace) {
  if (expr.kind == LogicalExpr::Kind::Get) {
    if (graph.RelationById(expr.relation_id).alias != sort_key.alias) {
      return nullptr;
    }
    auto scan = MakeScan(graph.RelationById(expr.relation_id), stats);
    ++trace.plans_costed;
    return MakeSort(scan, sort_key);
  }

  const auto predicates = graph.CrossingPredicates(expr.left_set, expr.right_set);
  PlanPtr best;
  for (const auto &predicate : predicates) {
    const bool sort_on_left = predicate.left == sort_key;
    const bool sort_on_right = predicate.right == sort_key;
    if (!sort_on_left && !sort_on_right) continue;

    const bool key_from_left_input = (expr.left_set & graph.MaskForAlias(sort_key.alias)) != 0;
    const auto other_key = sort_on_left ? predicate.right : predicate.left;
    auto sorted_left = OptimizeGroup(graph, stats, expr.left_set,
                                      RequiredProperty::Sorted(key_from_left_input ? sort_key : other_key),
                                      trace);
    auto sorted_right = OptimizeGroup(graph, stats, expr.right_set,
                                       RequiredProperty::Sorted(key_from_left_input ? other_key : sort_key),
                                       trace);
    if (sorted_left && sorted_right) {
      auto merge_join = MakeJoin(PhysicalOp::MergeJoin, sorted_left, sorted_right,
                                  graph, stats, &predicate, RequiredProperty::Sorted(sort_key));
      ++trace.plans_costed;
      if (Better(merge_join, best)) best = merge_join;
    }
  }

  return best;
}

} // namespace volcano
