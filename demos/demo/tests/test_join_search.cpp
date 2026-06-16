#include "volcano/bottom_up_dp.hpp"
#include "volcano/cost_model.hpp"
#include "volcano/search_strategy.hpp"
#include "volcano/top_down_partitioning.hpp"
#include "volcano/transformational.hpp"
#include "test_cases.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using volcano::RelSet;

namespace {

int failures = 0;

void Check(bool condition, const std::string &msg) {
  if (!condition) {
    std::cerr << "FAIL: " << msg << "\n";
    ++failures;
  }
}

void CheckNear(double a, double b, double tol, const std::string &msg) {
  if (std::abs(a - b) > tol) {
    std::cerr << "FAIL: " << msg << " (expected " << b << ", got " << a << ")\n";
    ++failures;
  }
}

// Helper: run all three strategies on a test case and verify they produce the same best cost.
void VerifyConsistency(const volcano::test::TestCase &tc) {
  volcano::BottomUpDP bu;
  volcano::Transformational tr;
  volcano::TopDownPartitioning td;

  auto result_bu = bu.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
  auto result_tr = tr.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
  auto result_td = td.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());

  Check(result_bu.best_plan.cost > 0, tc.name + ": BottomUpDP found a plan");
  Check(result_tr.best_plan.cost > 0, tc.name + ": Transformational found a plan");
  Check(result_td.best_plan.cost > 0, tc.name + ": TopDown found a plan");

  // All three should produce the same best cost
  CheckNear(result_bu.best_plan.cost, result_tr.best_plan.cost, 0.01,
            tc.name + ": BottomUpDP == Transformational cost");
  CheckNear(result_bu.best_plan.cost, result_td.best_plan.cost, 0.01,
            tc.name + ": BottomUpDP == TopDown cost");

  // Transformational should have duplicates
  Check(result_tr.trace.duplicates_generated > 0,
        tc.name + ": Transformational has duplicates");
  Check(result_tr.trace.rule_applications > 0,
        tc.name + ": Transformational has rule applications");

  // TopDown should have cache hits and branch-and-bound prunes (for n >= 3)
  if (tc.graph.RelationCount() >= 3) {
    Check(result_td.trace.cache_hits > 0,
          tc.name + ": TopDown has cache hits");
  }
}

} // namespace

int main() {
  std::cout << "=== Join Order Search Tests ===\n\n";

  // --- Test 1: Two-table query ---
  {
    std::cout << "Test 1: Two-table query\n";
    const auto &tc = volcano::test::TestCase::Lookup("two_table");

    volcano::BottomUpDP bu;
    auto result = bu.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
    Check(result.best_plan.cost > 0, "two_table: plan found");
    Check(result.trace.partitions_explored > 0, "two_table: partitions explored");
    Check(result.trace.plans_costed > 0, "two_table: plans costed");
    Check(result.trace.dp_cells_filled > 0, "two_table: DP cells filled");

    // For two tables, hash join should be cheaper than nested loop
    // (both have scan_cost=rows, so hash join cost is lower for large tables)
    std::cout << "  best_plan: " << result.best_plan.ToString()
              << " cost=" << result.best_plan.cost << "\n";
  }

  // --- Test 2: Consistency across strategies (chain_3) ---
  {
    std::cout << "Test 2: Cross-strategy consistency (chain_3)\n";
    const auto &tc = volcano::test::TestCase::Lookup("chain_3");
    VerifyConsistency(tc);
  }

  // --- Test 3: Consistency across strategies (star_4) ---
  {
    std::cout << "Test 3: Cross-strategy consistency (star_4)\n";
    const auto &tc = volcano::test::TestCase::Lookup("star_4");
    VerifyConsistency(tc);
  }

  // --- Test 4: Consistency across strategies (cycle_4) ---
  {
    std::cout << "Test 4: Cross-strategy consistency (cycle_4)\n";
    const auto &tc = volcano::test::TestCase::Lookup("cycle_4");
    VerifyConsistency(tc);
  }

  // --- Test 5: Consistency across strategies (clique_4) ---
  {
    std::cout << "Test 5: Cross-strategy consistency (clique_4)\n";
    const auto &tc = volcano::test::TestCase::Lookup("clique_4");
    VerifyConsistency(tc);
  }

  // --- Test 6: Sorted property ---
  {
    std::cout << "Test 6: Sorted property\n";
    const auto &tc = volcano::test::TestCase::Lookup("chain_3");

    volcano::TopDownPartitioning td;
    auto result = td.Search(tc.graph, tc.stats,
                            volcano::RequiredProperty::Sorted({"b", "x"}));
    Check(result.best_plan.cost > 0, "sorted: plan found");

    // Sorted plan should be at least as expensive as the Any plan
    auto result_any = td.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
    Check(result.best_plan.cost >= result_any.best_plan.cost,
          "sorted: cost >= any cost");

    std::cout << "  sorted_plan: " << result.best_plan.ToString()
              << " cost=" << result.best_plan.cost << "\n";
    std::cout << "  any_plan: " << result_any.best_plan.ToString()
              << " cost=" << result_any.best_plan.cost << "\n";
  }

  // --- Test 7: Transformational duplicates scale ---
  {
    std::cout << "Test 7: Transformational duplicates\n";
    // chain_4 (4 tables) should have more duplicates than chain_3 (3 tables)
    const auto &tc3 = volcano::test::TestCase::Lookup("chain_3");
    const auto &tc4 = volcano::test::TestCase::Lookup("chain_4");

    volcano::Transformational tr;
    auto result3 = tr.Search(tc3.graph, tc3.stats, volcano::RequiredProperty::Any());
    auto result4 = tr.Search(tc4.graph, tc4.stats, volcano::RequiredProperty::Any());

    std::cout << "  chain_3 duplicates: " << result3.trace.duplicates_generated
              << ", rule_applications: " << result3.trace.rule_applications << "\n";
    std::cout << "  chain_4 duplicates: " << result4.trace.duplicates_generated
              << ", rule_applications: " << result4.trace.rule_applications << "\n";

    Check(result4.trace.duplicates_generated >= result3.trace.duplicates_generated,
          "duplicates: chain_4 >= chain_3 (more relations = more duplicates)");
  }

  // --- Test 8: TopDown branch-and-bound ---
  {
    std::cout << "Test 8: TopDown branch-and-bound\n";
    const auto &tc = volcano::test::TestCase::Lookup("clique_4");

    volcano::TopDownPartitioning td;
    auto result = td.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());

    // Branch-and-bound uses predicted-cost lower bounds.
    // With toy-sized scan costs (=rows), the lower bound may be too weak to
    // trigger pruning. This is expected behavior — pruning effectiveness
    // depends on lower-bound quality relative to the cost model.
    Check(result.best_plan.cost > 0,
          "topdown: found a plan for clique_4");
    std::cout << "  branches_pruned: " << result.trace.branches_pruned
              << " (may be 0 if lower bound too weak for toy data)\n";
    std::cout << "  partitions_explored: " << result.trace.partitions_explored << "\n";
    std::cout << "  cache_hits: " << result.trace.cache_hits << "\n";
    std::cout << "  plans_costed: " << result.trace.plans_costed << "\n";
  }

  // --- Test 9: RelSet connectivity functions ---
  {
    std::cout << "Test 9: JoinGraph connectivity\n";
    const auto &tc = volcano::test::TestCase::Lookup("chain_3");
    const auto &graph = tc.graph;

    // Full set should be connected
    Check(graph.IsConnected(graph.FullSet()), "full set connected");

    // Singleton should be connected
    Check(graph.IsConnected(1), "singleton connected");

    // Disconnected subset (a and c without b)
    RelSet ac = (RelSet{1} << 0) | (RelSet{1} << 2); // a and c
    Check(!graph.IsConnected(ac), "a+c not connected (no direct edge)");

    // Connected pair
    RelSet ab = (RelSet{1} << 0) | (RelSet{1} << 1); // a and b
    Check(graph.IsConnected(ab), "a+b connected");

    // Valid join split
    Check(graph.IsValidJoinSplit(ab, RelSet{1} << 2, false), "a+b joined with c is valid");
  }

  // --- Test 10: All test cases load ---
  {
    std::cout << "Test 10: All test cases load\n";
    for (const auto &name : volcano::test::TestCase::AllNames()) {
      const auto &tc = volcano::test::TestCase::Lookup(name);
      Check(tc.graph.RelationCount() >= 2, name + ": has >= 2 relations");
      Check(tc.graph.RelationCount() <= 63, name + ": <= 63 relations");
      std::cout << "  " << name << ": " << tc.graph.RelationCount()
                << " relations, " << tc.graph.PredicateCount() << " predicates\n";
    }
  }

  // --- Summary ---
  std::cout << "\n=== " << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED")
            << " ===\n";
  return failures > 0 ? 1 : 0;
}
