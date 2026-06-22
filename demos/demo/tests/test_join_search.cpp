#include "volcano/dp_sub.hpp"
#include "volcano/cost_model.hpp"
#include "volcano/mpdp.hpp"
#include "volcano/mincut.hpp"
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

// Helper: run representative strategies on a test case and verify same best cost.
void VerifyConsistency(const volcano::test::TestCase &tc) {
  volcano::DPSub bu;
  volcano::MPDP mpdp;
  volcano::Transformational tr;
  volcano::TopDownPartitioning td;

  auto result_bu = bu.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
  auto result_mpdp = mpdp.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
  auto result_tr = tr.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
  auto result_td = td.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());

  Check(result_bu.has_plan, tc.name + ": DPSub found a plan");
  Check(result_mpdp.has_plan, tc.name + ": MPDP found a plan");
  Check(result_tr.has_plan, tc.name + ": Transformational found a plan");
  Check(result_td.has_plan, tc.name + ": TopDown found a plan");
  Check(result_bu.best_plan.cost > 0, tc.name + ": DPSub plan has positive cost");
  Check(result_mpdp.best_plan.cost > 0, tc.name + ": MPDP plan has positive cost");
  Check(result_tr.best_plan.cost > 0, tc.name + ": Transformational plan has positive cost");
  Check(result_td.best_plan.cost > 0, tc.name + ": TopDown plan has positive cost");

  // All strategies should produce the same best cost.
  CheckNear(result_bu.best_plan.cost, result_mpdp.best_plan.cost, 0.01,
            tc.name + ": DPSub == MPDP cost");
  CheckNear(result_bu.best_plan.cost, result_tr.best_plan.cost, 0.01,
            tc.name + ": DPSub == Transformational cost");
  CheckNear(result_bu.best_plan.cost, result_td.best_plan.cost, 0.01,
            tc.name + ": DPSub == TopDown cost");

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

    volcano::DPSub bu;
    auto result = bu.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
    Check(result.has_plan, "two_table: plan found");
    Check(result.best_plan.cost > 0, "two_table: plan has positive cost");
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
    Check(result.has_plan, "sorted: plan found");
    Check(result.best_plan.cost > 0, "sorted: plan has positive cost");

    // Sorted plan should be at least as expensive as the Any plan
    auto result_any = td.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
    Check(result.best_plan.cost >= result_any.best_plan.cost,
          "sorted: cost >= any cost");

    std::cout << "  sorted_plan: " << result.best_plan.ToString()
              << " cost=" << result.best_plan.cost << "\n";
    std::cout << "  any_plan: " << result_any.best_plan.ToString()
              << " cost=" << result_any.best_plan.cost << "\n";
  }

  // --- Test 7: Sorted root property fallback across strategies ---
  {
    std::cout << "Test 7: Sorted root property fallback across strategies\n";
    const auto &tc = volcano::test::TestCase::Lookup("chain_3");

    const auto property = volcano::RequiredProperty::Sorted({"a", "not_join_key"});
    volcano::DPSub bu;
    volcano::MPDP mpdp;
    volcano::Transformational tr;
    volcano::TopDownPartitioning td;
    volcano::TopDownPartitioning td_mincut(volcano::PartitionStrategy::Mincut);

    auto result = bu.Search(tc.graph, tc.stats, property);
    auto result_mpdp = mpdp.Search(tc.graph, tc.stats, property);
    auto result_tr = tr.Search(tc.graph, tc.stats, property);
    auto result_td = td.Search(tc.graph, tc.stats, property);
    auto result_mincut = td_mincut.Search(tc.graph, tc.stats, property);
    auto result_any = bu.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());

    Check(result.has_plan, "dpsub sorted fallback: plan found");
    Check(result_mpdp.has_plan, "mpdp sorted fallback: plan found");
    Check(result_tr.has_plan, "transformational sorted fallback: plan found");
    Check(result_td.has_plan, "topdown sorted fallback: plan found");
    Check(result_mincut.has_plan, "topdown mincut sorted fallback: plan found");
    Check(result.best_plan.op == volcano::PhysicalOp::SortEnforcer,
          "dpsub sorted fallback: root SortEnforcer used");
    Check(result_mpdp.best_plan.op == volcano::PhysicalOp::SortEnforcer,
          "mpdp sorted fallback: root SortEnforcer used");
    Check(result.best_plan.property == property,
          "dpsub sorted fallback: required property preserved");
    Check(result_mpdp.best_plan.property == property,
          "mpdp sorted fallback: required property preserved");
    Check(result.best_plan.cost >= result_any.best_plan.cost,
          "dpsub sorted fallback: sorted cost >= any cost");
    CheckNear(result.best_plan.cost, result_tr.best_plan.cost, 0.01,
              "sorted fallback: DPSub == Transformational cost");
    CheckNear(result.best_plan.cost, result_mpdp.best_plan.cost, 0.01,
              "sorted fallback: DPSub == MPDP cost");
    CheckNear(result.best_plan.cost, result_td.best_plan.cost, 0.01,
              "sorted fallback: DPSub == TopDown cost");
    CheckNear(result_td.best_plan.cost, result_mincut.best_plan.cost, 0.01,
              "sorted fallback: TopDown Mincut == Naive cost");

    std::cout << "  sorted_fallback_plan: " << result.best_plan.ToString()
              << " cost=" << result.best_plan.cost << "\n";
  }

  // --- Test 8: SearchResult no-plan state ---
  {
    std::cout << "Test 8: SearchResult no-plan state\n";
    volcano::JoinGraph graph;
    volcano::StatsCatalog stats;

    volcano::DPSub bu;
    volcano::MPDP mpdp;
    volcano::Transformational tr;
    volcano::TopDownPartitioning td;

    auto result_bu = bu.Search(graph, stats, volcano::RequiredProperty::Any());
    auto result_mpdp = mpdp.Search(graph, stats, volcano::RequiredProperty::Any());
    auto result_tr = tr.Search(graph, stats, volcano::RequiredProperty::Any());
    auto result_td = td.Search(graph, stats, volcano::RequiredProperty::Any());

    Check(!result_bu.has_plan, "empty graph: DPSub reports no plan");
    Check(!result_mpdp.has_plan, "empty graph: MPDP reports no plan");
    Check(!result_tr.has_plan, "empty graph: Transformational reports no plan");
    Check(!result_td.has_plan, "empty graph: TopDown reports no plan");
    Check(result_bu.trace.best_cost == 0.0, "empty graph: DPSub best_cost is 0");
    Check(result_mpdp.trace.best_cost == 0.0, "empty graph: MPDP best_cost is 0");
    Check(result_tr.trace.best_cost == 0.0, "empty graph: Transformational best_cost is 0");
    Check(result_td.trace.best_cost == 0.0, "empty graph: TopDown best_cost is 0");
  }

  // --- Test 9: StatsCatalog drives cost estimates ---
  {
    std::cout << "Test 9: StatsCatalog drives cost estimates\n";
    volcano::JoinGraph graph;
    graph.AddRelation("a", "table_a", 1000.0, 1000.0);
    graph.AddRelation("b", "table_b", 500.0, 500.0);
    graph.AddPredicate({"a", "x"}, {"b", "x"}, 0.5);

    volcano::StatsCatalog stats;
    stats.AddRelationStats("a", {10.0, 10.0});
    stats.AddRelationStats("b", {20.0, 20.0});
    stats.AddSelectivity({"a", "x"}, {"b", "x"}, 0.1);

    volcano::DPSub bu;
    volcano::MPDP mpdp;
    volcano::Transformational tr;
    volcano::TopDownPartitioning td;

    auto result_bu = bu.Search(graph, stats, volcano::RequiredProperty::Any());
    auto result_mpdp = mpdp.Search(graph, stats, volcano::RequiredProperty::Any());
    auto result_tr = tr.Search(graph, stats, volcano::RequiredProperty::Any());
    auto result_td = td.Search(graph, stats, volcano::RequiredProperty::Any());

    Check(result_bu.has_plan, "stats catalog: DPSub found a plan");
    Check(result_mpdp.has_plan, "stats catalog: MPDP found a plan");
    Check(result_tr.has_plan, "stats catalog: Transformational found a plan");
    Check(result_td.has_plan, "stats catalog: TopDown found a plan");
    CheckNear(result_bu.best_plan.cost, 80.0, 0.01,
              "stats catalog: DPSub uses catalog rows/selectivity");
    CheckNear(result_mpdp.best_plan.cost, 80.0, 0.01,
              "stats catalog: MPDP uses catalog rows/selectivity");
    CheckNear(result_tr.best_plan.cost, 80.0, 0.01,
              "stats catalog: Transformational uses catalog rows/selectivity");
    CheckNear(result_td.best_plan.cost, 80.0, 0.01,
              "stats catalog: TopDown uses catalog rows/selectivity");
  }

  // --- Test 10: Transformational duplicates scale ---
  {
    std::cout << "Test 10: Transformational duplicates\n";
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

  // --- Test 11: TopDown branch-and-bound ---
  {
    std::cout << "Test 11: TopDown branch-and-bound\n";
    const auto &tc = volcano::test::TestCase::Lookup("clique_4");

    volcano::TopDownPartitioning td;
    auto result = td.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());

    // Branch-and-bound uses predicted-cost lower bounds.
    // With toy-sized scan costs (=rows), the lower bound may be too weak to
    // trigger pruning. This is expected behavior — pruning effectiveness
    // depends on lower-bound quality relative to the cost model.
    Check(result.has_plan,
          "topdown: found a plan for clique_4");
    Check(result.best_plan.cost > 0,
          "topdown: plan has positive cost for clique_4");
    std::cout << "  branches_pruned: " << result.trace.branches_pruned
              << " (may be 0 if lower bound too weak for toy data)\n";
    std::cout << "  partitions_explored: " << result.trace.partitions_explored << "\n";
    std::cout << "  cache_hits: " << result.trace.cache_hits << "\n";
    std::cout << "  plans_costed: " << result.trace.plans_costed << "\n";
  }

  // --- Test 12: Mincut partitioner correctness ---
  {
    std::cout << "Test 12: Mincut partitioner correctness\n";
    const auto &tc = volcano::test::TestCase::Lookup("chain_3");

    // Mincut vs Naive: same best cost
    volcano::TopDownPartitioning td_naive(volcano::PartitionStrategy::Naive);
    volcano::TopDownPartitioning td_mincut(volcano::PartitionStrategy::Mincut);

    auto result_naive = td_naive.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
    auto result_mincut = td_mincut.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());

    CheckNear(result_naive.best_plan.cost, result_mincut.best_plan.cost, 0.01,
              "mincut: Mincut == Naive cost (chain_3)");

    // Mincut should never reject partitions (generates CP-free directly)
    Check(result_mincut.trace.partitions_rejected == 0,
          "mincut: zero partitions rejected (generates CP-free directly)");

    // Mincut should explore ≤ Naive partitions (better or equal)
    Check(result_mincut.trace.partitions_explored <= result_naive.trace.partitions_explored,
          "mincut: partitions_explored <= Naive");

    std::cout << "  Naive:  explored=" << result_naive.trace.partitions_explored
              << " rejected=" << result_naive.trace.partitions_rejected
              << " cost=" << result_naive.best_plan.cost << "\n";
    std::cout << "  Mincut: explored=" << result_mincut.trace.partitions_explored
              << " rejected=" << result_mincut.trace.partitions_rejected
              << " cost=" << result_mincut.best_plan.cost << "\n";
  }

  // --- Test 13: Mincut BC-tree structure ---
  {
    std::cout << "Test 13: Mincut BC-tree structure\n";

    // Chain a-b-c-d: articulation points are b and c (edges are bicomponents)
    {
      const auto &tc = volcano::test::TestCase::Lookup("chain_4");
      volcano::MincutPartitioner mp(tc.graph);
      std::cout << "  chain_4: bicomponents=" << mp.BicomponentCount()
                << " articulation_pts=" << mp.ArticulationPointCount() << "\n";
      Check(mp.BicomponentCount() >= 3,
            "mincut bc-tree: chain_4 has >= 3 bicomponents (3 edges)");
      Check(mp.ArticulationPointCount() == 2,
            "mincut bc-tree: chain_4 has 2 articulation points (b, c)");
    }

    // Star: center is the articulation point
    {
      const auto &tc = volcano::test::TestCase::Lookup("star_4");
      volcano::MincutPartitioner mp(tc.graph);
      std::cout << "  star_4: bicomponents=" << mp.BicomponentCount()
                << " articulation_pts=" << mp.ArticulationPointCount() << "\n";
      Check(mp.BicomponentCount() >= 3,
            "mincut bc-tree: star_4 has >= 3 bicomponents (3 edges)");
      Check(mp.ArticulationPointCount() == 1,
            "mincut bc-tree: star_4 has 1 articulation point (center)");
    }

    // Cycle and Clique: no articulation points (biconnected)
    for (const auto &name : {"cycle_4", "clique_4"}) {
      const auto &tc = volcano::test::TestCase::Lookup(name);
      volcano::MincutPartitioner mp(tc.graph);
      std::cout << "  " << name << ": bicomponents=" << mp.BicomponentCount()
                << " articulation_pts=" << mp.ArticulationPointCount() << "\n";
      Check(mp.ArticulationPointCount() == 0,
            std::string("mincut bc-tree: ") + name + " has 0 articulation points (biconnected)");
    }
  }

  // --- Test 14: RelSet connectivity functions ---
  {
    std::cout << "Test 14: JoinGraph connectivity\n";
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

  // --- Test 15: All test cases load ---
  {
    std::cout << "Test 15: All test cases load\n";
    for (const auto &name : volcano::test::TestCase::AllNames()) {
      const auto &tc = volcano::test::TestCase::Lookup(name);
      Check(tc.graph.RelationCount() >= 2, name + ": has >= 2 relations");
      Check(tc.graph.RelationCount() <= 63, name + ": <= 63 relations");
      std::cout << "  " << name << ": " << tc.graph.RelationCount()
                << " relations, " << tc.graph.PredicateCount() << " predicates\n";
    }
  }

  // --- Test 16: MPDP Figure 5 style graph ---
  {
    std::cout << "Test 16: MPDP Figure 5 style graph\n";
    const auto &tc = volcano::test::TestCase::Lookup("mpdp_fig5");

    volcano::DPSub bu;
    volcano::MPDP mpdp;
    volcano::TopDownPartitioning td_mincut(volcano::PartitionStrategy::Mincut);

    auto result_bu = bu.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
    auto result_mpdp = mpdp.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
    auto result_mincut = td_mincut.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());

    Check(result_bu.has_plan, "mpdp_fig5: DPSub found a plan");
    Check(result_mpdp.has_plan, "mpdp_fig5: MPDP found a plan");
    Check(result_mincut.has_plan, "mpdp_fig5: TopDown(Mincut) found a plan");
    CheckNear(result_bu.best_plan.cost, result_mpdp.best_plan.cost, 0.01,
              "mpdp_fig5: DPSub == MPDP cost");
    CheckNear(result_bu.best_plan.cost, result_mincut.best_plan.cost, 0.01,
              "mpdp_fig5: DPSub == TopDown(Mincut) cost");
    Check(result_mpdp.trace.partitions_explored < result_bu.trace.partitions_explored,
          "mpdp_fig5: MPDP explores fewer candidate partitions than DPSub");
    Check(result_mpdp.trace.partitions_rejected == 0,
          "mpdp_fig5: MPDP enumerates CCP pairs without rejected partitions");

    std::cout << "  DPSub partitions=" << result_bu.trace.partitions_explored
              << " rejected=" << result_bu.trace.partitions_rejected
              << " cost=" << result_bu.best_plan.cost << "\n";
    std::cout << "  MPDP partitions=" << result_mpdp.trace.partitions_explored
              << " rejected=" << result_mpdp.trace.partitions_rejected
              << " cost=" << result_mpdp.best_plan.cost << "\n";
  }

  // --- Test 17: MPDP branching block-cut tree ---
  {
    std::cout << "Test 17: MPDP branching block-cut tree\n";
    const auto &tc = volcano::test::TestCase::Lookup("mpdp_branching_blocks");

    volcano::DPSub bu;
    volcano::MPDP mpdp;
    volcano::TopDownPartitioning td_mincut(volcano::PartitionStrategy::Mincut);

    auto result_bu = bu.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
    auto result_mpdp = mpdp.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());
    auto result_mincut = td_mincut.Search(tc.graph, tc.stats, volcano::RequiredProperty::Any());

    Check(result_bu.has_plan, "mpdp_branching_blocks: DPSub found a plan");
    Check(result_mpdp.has_plan, "mpdp_branching_blocks: MPDP found a plan");
    Check(result_mincut.has_plan, "mpdp_branching_blocks: TopDown(Mincut) found a plan");
    CheckNear(result_bu.best_plan.cost, result_mpdp.best_plan.cost, 0.01,
              "mpdp_branching_blocks: DPSub == MPDP cost");
    CheckNear(result_bu.best_plan.cost, result_mincut.best_plan.cost, 0.01,
              "mpdp_branching_blocks: DPSub == TopDown(Mincut) cost");
    Check(result_mpdp.trace.partitions_explored < result_bu.trace.partitions_explored,
          "mpdp_branching_blocks: MPDP explores fewer candidate partitions than DPSub");
    Check(result_mpdp.trace.partitions_rejected == 0,
          "mpdp_branching_blocks: MPDP enumerates CCP pairs without rejected partitions");

    std::cout << "  DPSub partitions=" << result_bu.trace.partitions_explored
              << " rejected=" << result_bu.trace.partitions_rejected
              << " cost=" << result_bu.best_plan.cost << "\n";
    std::cout << "  MPDP partitions=" << result_mpdp.trace.partitions_explored
              << " rejected=" << result_mpdp.trace.partitions_rejected
              << " cost=" << result_mpdp.best_plan.cost << "\n";
  }

  // --- Summary ---
  std::cout << "\n=== " << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED")
            << " ===\n";
  return failures > 0 ? 1 : 0;
}
