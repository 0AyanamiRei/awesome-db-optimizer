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

} // namespace volcano::test
