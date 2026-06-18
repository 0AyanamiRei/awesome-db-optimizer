#include "volcano/cost_model.hpp"

#include <cmath>
#include <stdexcept>

namespace volcano {

namespace {

double CrossingSelectivityFromStats(const JoinGraph &graph,
                                    const StatsCatalog &stats,
                                    RelSet left,
                                    RelSet right) {
  double selectivity = 1.0;
  for (const auto &predicate : graph.CrossingPredicates(left, right)) {
    selectivity *= stats.LookupSelectivity(predicate.left, predicate.right);
  }
  return selectivity;
}

} // namespace

PlanPtr MakeScan(const Relation &relation, const StatsCatalog &stats) {
  const auto &relation_stats = stats.LookupRelation(relation.alias);
  auto plan = std::make_shared<PhysicalPlan>();
  plan->op = PhysicalOp::SeqScan;
  plan->relset = RelSet{1} << relation.id;
  plan->rows = relation_stats.rows;
  plan->cost = relation_stats.scan_cost;
  plan->property = RequiredProperty::Any();
  plan->relation_alias = relation.alias;
  return plan;
}

PlanPtr MakeSort(PlanPtr child, const ColumnRef &sort_key) {
  auto plan = std::make_shared<PhysicalPlan>();
  plan->op = PhysicalOp::SortEnforcer;
  plan->relset = child->relset;
  plan->rows = child->rows;
  plan->cost = child->cost + child->rows * std::log2(child->rows + 1.0);
  plan->property = RequiredProperty::Sorted(sort_key);
  plan->child = std::move(child);
  return plan;
}

PlanPtr MakeJoin(PhysicalOp op, PlanPtr left, PlanPtr right,
                 const JoinGraph &graph,
                 const StatsCatalog &stats,
                 const JoinPredicate *predicate,
                 const RequiredProperty &property) {
  auto plan = std::make_shared<PhysicalPlan>();
  plan->op = op;
  plan->relset = left->relset | right->relset;
  plan->rows = left->rows * right->rows *
               CrossingSelectivityFromStats(graph, stats, left->relset, right->relset);
  plan->property = property;
  plan->left = std::move(left);
  plan->right = std::move(right);
  if (predicate != nullptr) {
    plan->predicate = *predicate;
  }

  switch (op) {
  case PhysicalOp::HashJoin:
    plan->cost = plan->left->cost + plan->right->cost +
                 plan->left->rows + plan->right->rows + plan->rows;
    break;
  case PhysicalOp::NestedLoopJoin:
    plan->cost = plan->left->cost + plan->right->cost +
                 plan->left->rows * plan->right->rows;
    break;
  case PhysicalOp::MergeJoin:
    plan->cost = plan->left->cost + plan->right->cost +
                 plan->left->rows + plan->right->rows;
    break;
  default:
    throw std::runtime_error("MakeJoin called with non-join physical operator");
  }
  return plan;
}

} // namespace volcano
