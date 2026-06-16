#include "volcano/duckdb_sql_frontend.hpp"
#include "volcano/exporter.hpp"
#include "volcano/optimizer.hpp"
#include "volcano/stats.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace volcano;

struct TestFailure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    throw TestFailure(message);
  }
}

void ExpectNear(double actual, double expected, double epsilon, const std::string &message) {
  if (std::fabs(actual - expected) > epsilon) {
    std::ostringstream out;
    out << message << ": expected " << expected << ", got " << actual;
    throw TestFailure(out.str());
  }
}

void ExpectContains(const std::string &haystack, const std::string &needle, const std::string &message) {
  if (haystack.find(needle) == std::string::npos) {
    std::ostringstream out;
    out << message << ": expected to find '" << needle << "' in '" << haystack << "'";
    throw TestFailure(out.str());
  }
}

template <class Fn>
void ExpectThrows(Fn fn, const std::string &message) {
  try {
    fn();
  } catch (const std::exception &) {
    return;
  }
  throw TestFailure(message + ": expected exception");
}

StatsCatalog StatsFromJson(const std::string &json) {
  return StatsCatalog::FromJson(json);
}

JoinGraph ChainGraph() {
  JoinGraph graph;
  graph.AddRelation("a", "A", 1000.0, 0.0);
  graph.AddRelation("b", "B", 1000.0, 0.0);
  graph.AddRelation("c", "C", 1000.0, 0.0);
  graph.AddPredicate({"a", "id"}, {"b", "a_id"}, 0.001);
  graph.AddPredicate({"b", "id"}, {"c", "b_id"}, 0.5);
  return graph;
}

void TestStatsReversedSelectivityLookup() {
  auto stats = StatsFromJson(R"json(
    {
      "relations": {
        "o": { "rows": 100000, "scan_cost": 100000 },
        "c": { "rows": 1000, "scan_cost": 1000 }
      },
      "selectivities": [
        { "left": "o.customer_id", "right": "c.id", "selectivity": 0.001 }
      ]
    }
  )json");

  ExpectNear(stats.LookupSelectivity({"c", "id"}, {"o", "customer_id"}), 0.001, 1e-12,
             "reversed selectivity lookup should normalize edge direction");
}

void TestStatsMissingRelationFails() {
  auto stats = StatsFromJson(R"json(
    {
      "relations": {
        "a": { "rows": 10, "scan_cost": 10 }
      },
      "selectivities": []
    }
  )json");

  ExpectThrows([&] { (void)stats.LookupRelation("missing"); }, "missing relation stats should fail");
}

void TestSqlCommaWhereBuildsJoinGraph() {
  auto stats = StatsFromJson(R"json(
    {
      "relations": {
        "o": { "rows": 100000, "scan_cost": 100000 },
        "c": { "rows": 1000, "scan_cost": 1000 }
      },
      "selectivities": [
        { "left": "o.customer_id", "right": "c.id", "selectivity": 0.001 }
      ]
    }
  )json");

  auto graph = DuckDBSqlFrontend::ParseJoinGraph(
      "SELECT * FROM orders o, customers c WHERE o.customer_id = c.id", stats);

  Expect(graph.RelationCount() == 2, "comma FROM query should contain two relations");
  Expect(graph.PredicateCount() == 1, "WHERE equality should become one join predicate");
  Expect(graph.RelationByAlias("o").table_name == "orders", "table alias o should map to orders");
}

void TestSqlExplicitInnerJoinBuildsJoinGraph() {
  auto stats = StatsFromJson(R"json(
    {
      "relations": {
        "a": { "rows": 10, "scan_cost": 10 },
        "b": { "rows": 20, "scan_cost": 20 }
      },
      "selectivities": [
        { "left": "a.id", "right": "b.a_id", "selectivity": 0.01 }
      ]
    }
  )json");

  auto graph =
      DuckDBSqlFrontend::ParseJoinGraph("SELECT * FROM A a INNER JOIN B b ON a.id = b.a_id", stats);

  Expect(graph.RelationCount() == 2, "INNER JOIN query should contain two relations");
  Expect(graph.PredicateCount() == 1, "JOIN ON equality should become one join predicate");
}

void TestSqlRejectsUnsupportedShapes() {
  auto stats = StatsFromJson(R"json(
    {
      "relations": {
        "a": { "rows": 10, "scan_cost": 10 },
        "b": { "rows": 20, "scan_cost": 20 }
      },
      "selectivities": [
        { "left": "a.id", "right": "b.a_id", "selectivity": 0.01 }
      ]
    }
  )json");

  ExpectThrows([&] {
    (void)DuckDBSqlFrontend::ParseJoinGraph("SELECT * FROM A a LEFT JOIN B b ON a.id = b.a_id", stats);
  }, "outer joins should be rejected");

  ExpectThrows([&] {
    (void)DuckDBSqlFrontend::ParseJoinGraph("SELECT * FROM A a, B b WHERE a.id = b.a_id OR a.x = b.x", stats);
  }, "OR predicates should be rejected");
}

void TestOptimizerChoosesCheaperPhysicalJoin() {
  JoinGraph graph;
  graph.AddRelation("a", "A", 1.0, 0.0);
  graph.AddRelation("b", "B", 1.0, 0.0);
  graph.AddPredicate({"a", "id"}, {"b", "a_id"}, 1.0);

  Optimizer optimizer(graph, OptimizerOptions{.allow_cross_products = false});
  auto root = optimizer.BuildMemo();
  auto plan = optimizer.Optimize(root, RequiredProperty::Any());

  Expect(plan.has_value(), "two-table connected query should produce a plan");
  ExpectContains(plan->ToString(), "NestedLoopJoin", "tiny relation join should choose nested-loop cost");
}

void TestOptimizerPrefersSelectiveEdgeFirst() {
  Optimizer optimizer(ChainGraph(), OptimizerOptions{.allow_cross_products = false});
  auto root = optimizer.BuildMemo();
  auto plan = optimizer.Optimize(root, RequiredProperty::Any());

  Expect(plan.has_value(), "three-table chain should produce a plan");
  auto text = plan->ToString();
  const bool ab_left = text.find("HashJoin(SeqScan(a), SeqScan(b))") != std::string::npos;
  const bool ab_right = text.find("HashJoin(SeqScan(b), SeqScan(a))") != std::string::npos;
  Expect(ab_left || ab_right, "optimizer should join the selective a-b edge before adding c");
}

void TestMemoCountsDuplicateRuleAttempts() {
  Optimizer optimizer(ChainGraph(), OptimizerOptions{.allow_cross_products = false});
  (void)optimizer.BuildMemo();
  const auto trace = optimizer.Trace();

  Expect(trace.rule_attempts > 0, "memo exploration should apply transformation rules");
  Expect(trace.duplicate_expressions > 0, "commutativity should create duplicate attempts");
  Expect(trace.expression_count >= trace.group_count, "memo should contain expressions grouped by relation set");
}

void TestSortedPropertyUsesMergeOrSortEnforcer() {
  Optimizer optimizer(ChainGraph(), OptimizerOptions{.allow_cross_products = false});
  auto root = optimizer.BuildMemo();
  auto plan = optimizer.Optimize(root, RequiredProperty::Sorted({"a", "id"}));

  Expect(plan.has_value(), "sorted required property should produce a plan");
  auto text = plan->ToString();
  Expect(text.find("MergeJoin") != std::string::npos || text.find("Sort[") != std::string::npos,
         "sorted property should use merge join or add a sort enforcer");
  Expect(plan->property == RequiredProperty::Sorted({"a", "id"}), "plan should satisfy requested sorted property");
}

void TestCrossProductsRejectedByDefault() {
  JoinGraph graph;
  graph.AddRelation("a", "A", 10.0, 0.0);
  graph.AddRelation("b", "B", 10.0, 0.0);
  graph.AddRelation("c", "C", 10.0, 0.0);
  graph.AddPredicate({"a", "id"}, {"b", "a_id"}, 0.1);

  Optimizer optimizer(graph, OptimizerOptions{.allow_cross_products = false});
  auto root = optimizer.BuildMemo();
  auto plan = optimizer.Optimize(root, RequiredProperty::Any());

  Expect(!plan.has_value(), "disconnected full query should not produce a CP-free plan");

  Optimizer cp_optimizer(graph, OptimizerOptions{.allow_cross_products = true});
  auto cp_root = cp_optimizer.BuildMemo();
  auto cp_plan = cp_optimizer.Optimize(cp_root, RequiredProperty::Any());
  Expect(cp_plan.has_value(), "allow_cross_products=true should produce a plan for disconnected query");
}

void TestExportsWriteDotAndTraceJson() {
  Optimizer optimizer(ChainGraph(), OptimizerOptions{.allow_cross_products = false});
  auto root = optimizer.BuildMemo();
  auto plan = optimizer.Optimize(root, RequiredProperty::Any());
  Expect(plan.has_value(), "export test needs a best plan");

  const auto out_dir = std::filesystem::temp_directory_path() / "volcano_join_demo_export_test";
  std::filesystem::remove_all(out_dir);
  Exporter::WriteAll(out_dir, optimizer.Graph(), optimizer.Memo(), *plan, optimizer.Trace());

  for (const auto &name : {"join_graph.dot", "memo.dot", "best_plan.dot", "trace.json"}) {
    Expect(std::filesystem::exists(out_dir / name), std::string("missing export file ") + name);
  }

  std::ifstream trace_file(out_dir / "trace.json");
  std::stringstream trace;
  trace << trace_file.rdbuf();
  ExpectContains(trace.str(), "\"chosen_plan_cost\"", "trace JSON should include final cost");
}

using TestFn = void (*)();

struct NamedTest {
  const char *name;
  TestFn fn;
};

int RunAll(const std::vector<NamedTest> &tests) {
  int failed = 0;
  for (const auto &test : tests) {
    try {
      test.fn();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception &ex) {
      ++failed;
      std::cerr << "[FAIL] " << test.name << ": " << ex.what() << '\n';
    }
  }
  if (failed != 0) {
    std::cerr << failed << " test(s) failed\n";
    return 1;
  }
  std::cout << tests.size() << " test(s) passed\n";
  return 0;
}

} // namespace

int main() {
  return RunAll({
      {"stats reversed selectivity lookup", TestStatsReversedSelectivityLookup},
      {"stats missing relation fails", TestStatsMissingRelationFails},
      {"sql comma where builds join graph", TestSqlCommaWhereBuildsJoinGraph},
      {"sql explicit inner join builds join graph", TestSqlExplicitInnerJoinBuildsJoinGraph},
      {"sql rejects unsupported shapes", TestSqlRejectsUnsupportedShapes},
      {"optimizer chooses cheaper physical join", TestOptimizerChoosesCheaperPhysicalJoin},
      {"optimizer prefers selective edge first", TestOptimizerPrefersSelectiveEdgeFirst},
      {"memo counts duplicate rule attempts", TestMemoCountsDuplicateRuleAttempts},
      {"sorted property uses merge or sort enforcer", TestSortedPropertyUsesMergeOrSortEnforcer},
      {"cross products rejected by default", TestCrossProductsRejectedByDefault},
      {"exports write dot and trace json", TestExportsWriteDotAndTraceJson},
  });
}
