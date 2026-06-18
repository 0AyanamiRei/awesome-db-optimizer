#pragma once

#include "volcano/search_strategy.hpp"

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace volcano {

// Volcano/Cascades-style transformational search.
// Phase 1: BuildMemo — bottom-up logical enumeration + transformation rules.
// Phase 2: Top-down physical optimization on the pre-built memo.
//
// This strategy demonstrates the O(4^n) duplicate problem analyzed in
// Pellenkoft et al. (VLDB 1997).

// Logical expression stored in the memo.
struct LogicalExpr {
  enum class Kind { Get, Join };

  Kind kind = Kind::Get;
  RelSet relset = 0;
  std::size_t relation_id = 0;  // for Get
  RelSet left_set = 0;           // for Join
  RelSet right_set = 0;          // for Join

  static LogicalExpr Get(std::size_t relation_id);
  static LogicalExpr Join(RelSet left, RelSet right);

  std::string Key() const;
  std::string ToString(const JoinGraph &graph) const;
};

// Memo group: all logical expressions for a given relation set.
struct MemoGroup {
  RelSet relset = 0;
  std::vector<LogicalExpr> expressions;
};

// Memo store with deduplication.
class MemoStore {
public:
  void Clear();
  bool InsertExpression(const LogicalExpr &expr, SearchTrace &trace, bool count_duplicate = true);
  const MemoGroup *FindGroup(RelSet relset) const;
  const std::vector<MemoGroup> &Groups() const;
  std::size_t GroupCount() const;
  std::size_t ExpressionCount() const;

private:
  MemoGroup &EnsureGroup(RelSet relset);
  std::vector<MemoGroup> groups_;
  std::unordered_map<RelSet, std::size_t> group_by_relset_;
  std::unordered_set<std::string> expression_keys_;
};

class Transformational : public SearchStrategy {
public:
  std::string Name() const override { return "Transformational"; }
  SearchResult Search(const JoinGraph &graph,
                      const StatsCatalog &stats,
                      const RequiredProperty &property) override;

private:
  bool TryInsertJoin(const JoinGraph &graph, bool allow_cp,
                     RelSet left, RelSet right, bool from_rule,
                     std::deque<LogicalExpr> &work, SearchTrace &trace);
  void SeedInitialExpressions(const JoinGraph &graph, bool allow_cp,
                              std::deque<LogicalExpr> &work, SearchTrace &trace);
  void ExploreTransformations(const JoinGraph &graph, bool allow_cp,
                              std::deque<LogicalExpr> &work, SearchTrace &trace);
  void ApplyRules(const JoinGraph &graph, bool allow_cp,
                  const LogicalExpr &expr, std::deque<LogicalExpr> &work,
                  SearchTrace &trace);

  // Physical optimization on the memo.
  PlanPtr OptimizeGroup(const JoinGraph &graph, const StatsCatalog &stats, RelSet relset,
                        const RequiredProperty &property, SearchTrace &trace);
  PlanPtr OptimizeAny(const JoinGraph &graph, const StatsCatalog &stats,
                      RelSet relset, SearchTrace &trace);
  PlanPtr OptimizeSorted(const JoinGraph &graph, const StatsCatalog &stats, RelSet relset,
                         const ColumnRef &sort_key, SearchTrace &trace);
  PlanPtr OptimizeAnyExpr(const JoinGraph &graph, const StatsCatalog &stats,
                          const LogicalExpr &expr,
                          SearchTrace &trace);
  PlanPtr OptimizeSortedExpr(const JoinGraph &graph, const StatsCatalog &stats,
                             const LogicalExpr &expr,
                             const ColumnRef &sort_key, SearchTrace &trace);

  std::string CacheKey(RelSet relset, const RequiredProperty &property) const;

  MemoStore memo_;
  std::unordered_map<std::string, PlanPtr> best_cache_;
};

} // namespace volcano
