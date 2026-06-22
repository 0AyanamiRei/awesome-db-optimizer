#include "test_cases.hpp"

#include <stdexcept>
#include <unordered_map>

namespace volcano::test {
namespace {

std::unordered_map<std::string, TestCase> &Registry() {
  static std::unordered_map<std::string, TestCase> registry;
  return registry;
}

void Register(TestCase tc) {
  auto name = tc.name;
  Registry().emplace(std::move(name), std::move(tc));
}

struct Init {
  Init() {
    Register(MakeChain3());
    Register(MakeChain4());
    Register(MakeStar4());
    Register(MakeCycle4());
    Register(MakeClique4());
    Register(MakeMPDPFigure5());
    Register(MakeMPDPBranchingBlocks());
    Register(MakeTwoTable());
  }
};

[[maybe_unused]] Init init;

} // namespace

const TestCase &TestCase::Lookup(const std::string &name) {
  const auto &reg = Registry();
  auto found = reg.find(name);
  if (found == reg.end()) {
    throw std::runtime_error("unknown test case: " + name);
  }
  return found->second;
}

std::vector<std::string> TestCase::AllNames() {
  std::vector<std::string> names;
  for (const auto &[name, _] : Registry()) {
    names.push_back(name);
  }
  return names;
}

// --- Implementations ---

TestCase MakeTwoTable() {
  TestCase tc;
  tc.name = "two_table";
  tc.description = "Two tables: a(1000 rows) JOIN b(100 rows) ON a.x=b.x, sel=0.01";

  tc.graph.AddRelation("a", "table_a", 1000.0, 1000.0);
  tc.graph.AddRelation("b", "table_b", 100.0, 100.0);
  tc.graph.AddPredicate({"a", "x"}, {"b", "x"}, 0.01);

  tc.stats.AddRelationStats("a", {1000.0, 1000.0});
  tc.stats.AddRelationStats("b", {100.0, 100.0});
  tc.stats.AddSelectivity({"a", "x"}, {"b", "x"}, 0.01);

  return tc;
}

TestCase MakeChain3() {
  TestCase tc;
  tc.name = "chain_3";
  tc.description = "Chain: a(1000)-b(100)-c(500), a.x=b.x(0.01), b.y=c.y(0.05)";

  tc.graph.AddRelation("a", "table_a", 1000.0, 1000.0);
  tc.graph.AddRelation("b", "table_b", 100.0, 100.0);
  tc.graph.AddRelation("c", "table_c", 500.0, 500.0);
  tc.graph.AddPredicate({"a", "x"}, {"b", "x"}, 0.01);
  tc.graph.AddPredicate({"b", "y"}, {"c", "y"}, 0.05);

  tc.stats.AddRelationStats("a", {1000.0, 1000.0});
  tc.stats.AddRelationStats("b", {100.0, 100.0});
  tc.stats.AddRelationStats("c", {500.0, 500.0});
  tc.stats.AddSelectivity({"a", "x"}, {"b", "x"}, 0.01);
  tc.stats.AddSelectivity({"b", "y"}, {"c", "y"}, 0.05);

  return tc;
}

TestCase MakeChain4() {
  TestCase tc;
  tc.name = "chain_4";
  tc.description = "Chain: a-b-c-d, 4 tables";

  tc.graph.AddRelation("a", "table_a", 1000.0, 1000.0);
  tc.graph.AddRelation("b", "table_b", 100.0, 100.0);
  tc.graph.AddRelation("c", "table_c", 500.0, 500.0);
  tc.graph.AddRelation("d", "table_d", 200.0, 200.0);
  tc.graph.AddPredicate({"a", "x"}, {"b", "x"}, 0.01);
  tc.graph.AddPredicate({"b", "y"}, {"c", "y"}, 0.05);
  tc.graph.AddPredicate({"c", "z"}, {"d", "z"}, 0.02);

  tc.stats.AddRelationStats("a", {1000.0, 1000.0});
  tc.stats.AddRelationStats("b", {100.0, 100.0});
  tc.stats.AddRelationStats("c", {500.0, 500.0});
  tc.stats.AddRelationStats("d", {200.0, 200.0});
  tc.stats.AddSelectivity({"a", "x"}, {"b", "x"}, 0.01);
  tc.stats.AddSelectivity({"b", "y"}, {"c", "y"}, 0.05);
  tc.stats.AddSelectivity({"c", "z"}, {"d", "z"}, 0.02);

  return tc;
}

TestCase MakeStar4() {
  TestCase tc;
  tc.name = "star_4";
  tc.description = "Star: center(10000) connected to a(100), b(200), c(300)";

  tc.graph.AddRelation("center", "fact", 10000.0, 10000.0);
  tc.graph.AddRelation("a", "dim_a", 100.0, 100.0);
  tc.graph.AddRelation("b", "dim_b", 200.0, 200.0);
  tc.graph.AddRelation("c", "dim_c", 300.0, 300.0);
  tc.graph.AddPredicate({"center", "x"}, {"a", "x"}, 0.001);
  tc.graph.AddPredicate({"center", "y"}, {"b", "y"}, 0.002);
  tc.graph.AddPredicate({"center", "z"}, {"c", "z"}, 0.003);

  tc.stats.AddRelationStats("center", {10000.0, 10000.0});
  tc.stats.AddRelationStats("a", {100.0, 100.0});
  tc.stats.AddRelationStats("b", {200.0, 200.0});
  tc.stats.AddRelationStats("c", {300.0, 300.0});
  tc.stats.AddSelectivity({"center", "x"}, {"a", "x"}, 0.001);
  tc.stats.AddSelectivity({"center", "y"}, {"b", "y"}, 0.002);
  tc.stats.AddSelectivity({"center", "z"}, {"c", "z"}, 0.003);

  return tc;
}

TestCase MakeCycle4() {
  TestCase tc;
  tc.name = "cycle_4";
  tc.description = "Cycle: a-b-c-d-a, all rows=100, all sel=0.1";

  for (const auto &[alias, tbl] : {
         std::pair{"a", "table_a"}, {"b", "table_b"},
         {"c", "table_c"}, {"d", "table_d"}}) {
    tc.graph.AddRelation(alias, tbl, 100.0, 100.0);
  }
  tc.graph.AddPredicate({"a", "x"}, {"b", "x"}, 0.1);
  tc.graph.AddPredicate({"b", "y"}, {"c", "y"}, 0.1);
  tc.graph.AddPredicate({"c", "z"}, {"d", "z"}, 0.1);
  tc.graph.AddPredicate({"d", "w"}, {"a", "w"}, 0.1);

  for (const auto &alias : {"a", "b", "c", "d"}) {
    tc.stats.AddRelationStats(alias, {100.0, 100.0});
  }
  tc.stats.AddSelectivity({"a", "x"}, {"b", "x"}, 0.1);
  tc.stats.AddSelectivity({"b", "y"}, {"c", "y"}, 0.1);
  tc.stats.AddSelectivity({"c", "z"}, {"d", "z"}, 0.1);
  tc.stats.AddSelectivity({"d", "w"}, {"a", "w"}, 0.1);

  return tc;
}

TestCase MakeClique4() {
  TestCase tc;
  tc.name = "clique_4";
  tc.description = "Clique: 4 fully-connected tables, all rows=100, all sel=0.1";

  for (const auto &[alias, tbl] : {
         std::pair{"a", "table_a"}, {"b", "table_b"},
         {"c", "table_c"}, {"d", "table_d"}}) {
    tc.graph.AddRelation(alias, tbl, 100.0, 100.0);
  }
  // Full clique: all 6 edges
  tc.graph.AddPredicate({"a", "x"}, {"b", "x"}, 0.1);
  tc.graph.AddPredicate({"a", "y"}, {"c", "y"}, 0.1);
  tc.graph.AddPredicate({"a", "z"}, {"d", "z"}, 0.1);
  tc.graph.AddPredicate({"b", "u"}, {"c", "u"}, 0.1);
  tc.graph.AddPredicate({"b", "v"}, {"d", "v"}, 0.1);
  tc.graph.AddPredicate({"c", "w"}, {"d", "w"}, 0.1);

  for (const auto &alias : {"a", "b", "c", "d"}) {
    tc.stats.AddRelationStats(alias, {100.0, 100.0});
  }
  tc.stats.AddSelectivity({"a", "x"}, {"b", "x"}, 0.1);
  tc.stats.AddSelectivity({"a", "y"}, {"c", "y"}, 0.1);
  tc.stats.AddSelectivity({"a", "z"}, {"d", "z"}, 0.1);
  tc.stats.AddSelectivity({"b", "u"}, {"c", "u"}, 0.1);
  tc.stats.AddSelectivity({"b", "v"}, {"d", "v"}, 0.1);
  tc.stats.AddSelectivity({"c", "w"}, {"d", "w"}, 0.1);

  return tc;
}

TestCase MakeMPDPFigure5() {
  TestCase tc;
  tc.name = "mpdp_fig5";
  tc.description = "MPDP Figure 5 style: two cyclic blocks connected by bridge blocks";

  for (std::size_t i = 1; i <= 9; ++i) {
    const auto alias = "r" + std::to_string(i);
    tc.graph.AddRelation(alias, "table_" + alias, 100.0 + static_cast<double>(i * 10),
                         100.0 + static_cast<double>(i * 10));
    tc.stats.AddRelationStats(alias, {100.0 + static_cast<double>(i * 10),
                                      100.0 + static_cast<double>(i * 10)});
  }

  tc.graph.AddPredicate({"r1", "a"}, {"r2", "a"}, 0.10);
  tc.graph.AddPredicate({"r2", "b"}, {"r3", "b"}, 0.08);
  tc.graph.AddPredicate({"r3", "c"}, {"r4", "c"}, 0.07);
  tc.graph.AddPredicate({"r4", "d"}, {"r1", "d"}, 0.06);
  tc.graph.AddPredicate({"r4", "e"}, {"r5", "e"}, 0.05);
  tc.graph.AddPredicate({"r5", "f"}, {"r9", "f"}, 0.04);
  tc.graph.AddPredicate({"r6", "g"}, {"r7", "g"}, 0.10);
  tc.graph.AddPredicate({"r7", "h"}, {"r8", "h"}, 0.08);
  tc.graph.AddPredicate({"r8", "i"}, {"r9", "i"}, 0.07);
  tc.graph.AddPredicate({"r9", "j"}, {"r6", "j"}, 0.06);

  tc.stats.AddSelectivity({"r1", "a"}, {"r2", "a"}, 0.10);
  tc.stats.AddSelectivity({"r2", "b"}, {"r3", "b"}, 0.08);
  tc.stats.AddSelectivity({"r3", "c"}, {"r4", "c"}, 0.07);
  tc.stats.AddSelectivity({"r4", "d"}, {"r1", "d"}, 0.06);
  tc.stats.AddSelectivity({"r4", "e"}, {"r5", "e"}, 0.05);
  tc.stats.AddSelectivity({"r5", "f"}, {"r9", "f"}, 0.04);
  tc.stats.AddSelectivity({"r6", "g"}, {"r7", "g"}, 0.10);
  tc.stats.AddSelectivity({"r7", "h"}, {"r8", "h"}, 0.08);
  tc.stats.AddSelectivity({"r8", "i"}, {"r9", "i"}, 0.07);
  tc.stats.AddSelectivity({"r9", "j"}, {"r6", "j"}, 0.06);

  return tc;
}

TestCase MakeMPDPBranchingBlocks() {
  TestCase tc;
  tc.name = "mpdp_branching_blocks";
  tc.description = "MPDP branching block-cut tree: one cut vertex shared by three cycles";

  for (const auto &[alias, rows] : {
         std::pair{"c", 500.0}, {"a1", 120.0}, {"a2", 130.0},
         {"b1", 140.0}, {"b2", 150.0}, {"d1", 160.0}, {"d2", 170.0}}) {
    tc.graph.AddRelation(alias, "table_" + std::string(alias), rows, rows);
    tc.stats.AddRelationStats(alias, {rows, rows});
  }

  tc.graph.AddPredicate({"c", "a"}, {"a1", "a"}, 0.05);
  tc.graph.AddPredicate({"a1", "b"}, {"a2", "b"}, 0.04);
  tc.graph.AddPredicate({"a2", "c"}, {"c", "c"}, 0.03);
  tc.graph.AddPredicate({"c", "d"}, {"b1", "d"}, 0.06);
  tc.graph.AddPredicate({"b1", "e"}, {"b2", "e"}, 0.04);
  tc.graph.AddPredicate({"b2", "f"}, {"c", "f"}, 0.03);
  tc.graph.AddPredicate({"c", "g"}, {"d1", "g"}, 0.07);
  tc.graph.AddPredicate({"d1", "h"}, {"d2", "h"}, 0.04);
  tc.graph.AddPredicate({"d2", "i"}, {"c", "i"}, 0.03);

  tc.stats.AddSelectivity({"c", "a"}, {"a1", "a"}, 0.05);
  tc.stats.AddSelectivity({"a1", "b"}, {"a2", "b"}, 0.04);
  tc.stats.AddSelectivity({"a2", "c"}, {"c", "c"}, 0.03);
  tc.stats.AddSelectivity({"c", "d"}, {"b1", "d"}, 0.06);
  tc.stats.AddSelectivity({"b1", "e"}, {"b2", "e"}, 0.04);
  tc.stats.AddSelectivity({"b2", "f"}, {"c", "f"}, 0.03);
  tc.stats.AddSelectivity({"c", "g"}, {"d1", "g"}, 0.07);
  tc.stats.AddSelectivity({"d1", "h"}, {"d2", "h"}, 0.04);
  tc.stats.AddSelectivity({"d2", "i"}, {"c", "i"}, 0.03);

  return tc;
}

} // namespace volcano::test
