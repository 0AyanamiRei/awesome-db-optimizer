#pragma once

#include "volcano/types.hpp"

#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace volcano {

struct LogicalExpr {
  enum class Kind { Get, Join };

  Kind kind = Kind::Get;
  RelSet relset = 0;
  std::size_t relation_id = 0;
  RelSet left_set = 0;
  RelSet right_set = 0;

  static LogicalExpr Get(std::size_t relation_id);
  static LogicalExpr Join(RelSet left, RelSet right);

  std::string Key() const;
  std::string ToString(const JoinGraph &graph) const;
};

struct MemoGroup {
  RelSet relset = 0;
  std::vector<LogicalExpr> expressions;
};

class MemoStore {
public:
  void Clear();
  bool InsertExpression(const LogicalExpr &expr, TraceCounters &trace, bool count_duplicate = true);
  const MemoGroup *FindGroup(RelSet relset) const;
  const std::vector<MemoGroup> &Groups() const;
  std::size_t GroupCount() const;
  std::size_t ExpressionCount() const;

private:
  MemoGroup &EnsureGroup(RelSet relset);

  std::vector<MemoGroup> groups_;
  std::unordered_map<RelSet, GroupId> group_by_relset_;
  std::unordered_set<std::string> expression_keys_;
};

struct OptimizerOptions {
  bool allow_cross_products = false;
};

class Optimizer {
public:
  Optimizer(JoinGraph graph, OptimizerOptions options);

  RelSet BuildMemo();
  std::optional<PhysicalPlan> Optimize(RelSet root, const RequiredProperty &property);

  const JoinGraph &Graph() const;
  const MemoStore &Memo() const;
  TraceCounters Trace() const;

private:
  bool TryInsertJoin(RelSet left, RelSet right, bool from_rule, std::deque<LogicalExpr> &work);
  void SeedInitialExpressions(std::deque<LogicalExpr> &work);
  void ExploreTransformations(std::deque<LogicalExpr> &work);
  void ApplyRules(const LogicalExpr &expr, std::deque<LogicalExpr> &work);

  std::optional<PlanPtr> OptimizeGroup(RelSet relset, const RequiredProperty &property);
  std::optional<PlanPtr> OptimizeAny(RelSet relset);
  std::optional<PlanPtr> OptimizeSorted(RelSet relset, const ColumnRef &sort_key);
  std::optional<PlanPtr> OptimizeAnyExpr(const LogicalExpr &expr);
  std::optional<PlanPtr> OptimizeSortedExpr(const LogicalExpr &expr, const ColumnRef &sort_key);
  PlanPtr MakeScan(const Relation &relation) const;
  PlanPtr MakeSort(PlanPtr child, const ColumnRef &sort_key) const;
  PlanPtr MakeJoin(PhysicalOp op, PlanPtr left, PlanPtr right, const JoinPredicate *predicate,
                   const RequiredProperty &property) const;
  std::string CacheKey(RelSet relset, const RequiredProperty &property) const;

  JoinGraph graph_;
  OptimizerOptions options_;
  MemoStore memo_;
  TraceCounters trace_;
  std::unordered_map<std::string, std::optional<PlanPtr>> best_cache_;
  bool memo_built_ = false;
};

} // namespace volcano
