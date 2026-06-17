#include "volcano/dp_sub.hpp"
#include "volcano/exporter.hpp"
#include "volcano/search_strategy.hpp"
#include "volcano/top_down_partitioning.hpp"
#include "volcano/transformational.hpp"
#include "test_cases.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void PrintUsage() {
  std::cout << "Usage: volcano_join_demo [OPTIONS]\n"
            << "\n"
            << "  --strategy <name>   Run a single strategy\n"
            << "                      Choices: dpsub, transform, topdown\n"
            << "  --test <name>       Test case to run\n"
            << "  --compare           Run all strategies and output comparison\n"
            << "  --out <dir>         Output directory for DOT/JSON exports\n"
            << "  --list              List all available test cases\n"
            << "  --help, -h          Show this help\n"
            << "\n"
            << "Examples:\n"
            << "  volcano_join_demo --list\n"
            << "  volcano_join_demo --strategy topdown --test chain_3\n"
            << "  volcano_join_demo --compare --test cycle_4 --out /tmp/out\n";
}

volcano::RequiredProperty ParseRequired(const std::string &text) {
  if (text == "any") return volcano::RequiredProperty::Any();
  constexpr std::string_view prefix = "sorted:";
  if (text.rfind(prefix, 0) == 0) {
    const auto column = text.substr(prefix.size());
    const auto dot = column.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= column.size()) {
      throw std::runtime_error("--required sorted form must be sorted:alias.column");
    }
    return volcano::RequiredProperty::Sorted(
        {std::string(column.substr(0, dot)), std::string(column.substr(dot + 1))});
  }
  throw std::runtime_error("--required must be either 'any' or 'sorted:alias.column'");
}

std::unique_ptr<volcano::SearchStrategy> MakeStrategy(const std::string &name) {
  if (name == "dpsub") return std::make_unique<volcano::DPSub>();
  if (name == "transform") return std::make_unique<volcano::Transformational>();
  if (name == "topdown")
    return std::make_unique<volcano::TopDownPartitioning>(
        volcano::PartitionStrategy::Naive);
  if (name == "topdown-mincut")
    return std::make_unique<volcano::TopDownPartitioning>(
        volcano::PartitionStrategy::Mincut);
  throw std::runtime_error("unknown strategy: " + name +
                           " (choices: dpsub, transform, topdown, topdown-mincut)");
}

void PrintTraceTable(const std::vector<std::pair<std::string, volcano::SearchTrace>> &results) {
  // Header
  std::cout << std::left
            << std::setw(20) << "Strategy"
            << std::setw(14) << "Partitions"
            << std::setw(14) << "Rejected"
            << std::setw(14) << "PlansCosted"
            << std::setw(14) << "CacheHits"
            << std::setw(14) << "Cached"
            << std::setw(14) << "Duplicates"
            << std::setw(14) << "Rules"
            << std::setw(14) << "DPCells"
            << std::setw(14) << "Pruned"
            << std::setw(16) << "BestCost"
            << "\n";
  std::cout << std::string(20 + 14*10 + 16, '-') << "\n";

  for (const auto &[name, trace] : results) {
    std::cout << std::left
              << std::setw(20) << name
              << std::setw(14) << trace.partitions_explored
              << std::setw(14) << trace.partitions_rejected
              << std::setw(14) << trace.plans_costed
              << std::setw(14) << trace.cache_hits
              << std::setw(14) << trace.plans_cached
              << std::setw(14) << trace.duplicates_generated
              << std::setw(14) << trace.rule_applications
              << std::setw(14) << trace.dp_cells_filled
              << std::setw(14) << trace.branches_pruned
              << std::setw(16) << std::fixed << std::setprecision(2) << trace.best_cost
              << "\n";
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::string strategy_name;
    std::string test_name;
    std::string out_dir;
    std::string required_str = "any";
    bool compare = false;
    bool list = false;

    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--help" || arg == "-h") {
        PrintUsage();
        return 0;
      }
      if (arg == "--compare") {
        compare = true;
        continue;
      }
      if (arg == "--list") {
        list = true;
        continue;
      }
      if (arg == "--strategy" || arg == "--test" || arg == "--out" || arg == "--required") {
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + arg);
        const std::string value = argv[++i];
        if (arg == "--strategy") strategy_name = value;
        else if (arg == "--test") test_name = value;
        else if (arg == "--out") out_dir = value;
        else if (arg == "--required") required_str = value;
        continue;
      }
      throw std::runtime_error("unknown argument: " + arg);
    }

    if (list) {
      std::cout << "Available test cases:\n";
      for (const auto &name : volcano::test::TestCase::AllNames()) {
        const auto &tc = volcano::test::TestCase::Lookup(name);
        std::cout << "  " << name << " — " << tc.description << "\n";
      }
      return 0;
    }

    if (test_name.empty()) {
      throw std::runtime_error("--test is required (use --list to see available tests)");
    }

    const auto &tc = volcano::test::TestCase::Lookup(test_name);
    const auto property = ParseRequired(required_str);

    if (compare) {
      // Run all strategies and compare
      std::vector<std::string> strategy_names = {"dpsub", "transform", "topdown"};
      std::vector<std::pair<std::string, volcano::SearchTrace>> results;

      std::cout << "Test: " << test_name << " (" << tc.description << ")\n";
      std::cout << "Relations: " << tc.graph.RelationCount()
                << ", Predicates: " << tc.graph.PredicateCount() << "\n";
      std::cout << "Required property: " << property.ToString() << "\n\n";

      for (const auto &name : strategy_names) {
        auto strategy = MakeStrategy(name);
        auto result = strategy->Search(tc.graph, tc.stats, property);
        results.emplace_back(strategy->Name(), result.trace);

        // Also export individual traces if --out is given
        if (!out_dir.empty()) {
          volcano::Exporter::WriteDotFiles(std::filesystem::path(out_dir) / name,
                                           tc.graph, result.best_plan);
          volcano::Exporter::WriteTrace(std::filesystem::path(out_dir) / name,
                                         strategy->Name(), result.trace);
        }
      }

      PrintTraceTable(results);

      if (!out_dir.empty()) {
        volcano::Exporter::WriteComparison(out_dir, test_name, results);
        std::cout << "\nExports written to: " << out_dir << "\n";
      }
    } else {
      // Run a single strategy
      if (strategy_name.empty()) {
        throw std::runtime_error("--strategy is required (or use --compare)");
      }

      auto strategy = MakeStrategy(strategy_name);
      auto result = strategy->Search(tc.graph, tc.stats, property);

      std::cout << "Strategy: " << strategy->Name() << "\n";
      std::cout << "Test: " << test_name << " (" << tc.description << ")\n";
      std::cout << "Relations: " << tc.graph.RelationCount()
                << ", Predicates: " << tc.graph.PredicateCount() << "\n";
      std::cout << "Required property: " << property.ToString() << "\n\n";

      std::cout << "Best plan: " << result.best_plan.ToString() << "\n";
      std::cout << "Cost: " << result.best_plan.cost << "\n\n";

      PrintTraceTable({{strategy->Name(), result.trace}});

      if (!out_dir.empty()) {
        volcano::Exporter::WriteDotFiles(out_dir, tc.graph, result.best_plan);
        volcano::Exporter::WriteTrace(out_dir, strategy->Name(), result.trace);
        std::cout << "\nExports written to: " << out_dir << "\n";
      }
    }

    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "error: " << ex.what() << "\n";
    PrintUsage();
    return 1;
  }
}
