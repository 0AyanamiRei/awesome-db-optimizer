#include "volcano/duckdb_sql_frontend.hpp"
#include "volcano/exporter.hpp"
#include "volcano/optimizer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

std::string ReadFile(const std::string &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open file: " + path);
  }
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

volcano::RequiredProperty ParseRequired(const std::string &text) {
  if (text == "any") {
    return volcano::RequiredProperty::Any();
  }
  constexpr std::string_view prefix = "sorted:";
  if (text.rfind(prefix, 0) == 0) {
    const auto column = text.substr(prefix.size());
    const auto dot = column.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= column.size()) {
      throw std::runtime_error("--required sorted form must be sorted:alias.column");
    }
    return volcano::RequiredProperty::Sorted({column.substr(0, dot), column.substr(dot + 1)});
  }
  throw std::runtime_error("--required must be either 'any' or 'sorted:alias.column'");
}

void PrintUsage() {
  std::cout << "Usage: volcano_join_demo --sql query.sql --stats stats.json --required any --out out/ "
               "[--allow-cp]\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::unordered_map<std::string, std::string> args;
    bool allow_cp = false;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--help" || arg == "-h") {
        PrintUsage();
        return 0;
      }
      if (arg == "--allow-cp") {
        allow_cp = true;
        continue;
      }
      if (arg == "--sql" || arg == "--stats" || arg == "--required" || arg == "--out") {
        if (i + 1 >= argc) {
          throw std::runtime_error("missing value for " + arg);
        }
        args[arg] = argv[++i];
        continue;
      }
      throw std::runtime_error("unknown argument: " + arg);
    }

    for (const auto *required : {"--sql", "--stats", "--out"}) {
      if (args.find(required) == args.end()) {
        throw std::runtime_error(std::string("missing required argument: ") + required);
      }
    }
    if (args.find("--required") == args.end()) {
      args["--required"] = "any";
    }

    const auto stats = volcano::StatsCatalog::FromFile(args.at("--stats"));
    const auto sql = ReadFile(args.at("--sql"));
    auto graph = volcano::DuckDBSqlFrontend::ParseJoinGraph(sql, stats);
    volcano::Optimizer optimizer(std::move(graph), volcano::OptimizerOptions{.allow_cross_products = allow_cp});
    const auto root = optimizer.BuildMemo();
    auto plan = optimizer.Optimize(root, ParseRequired(args.at("--required")));
    if (!plan.has_value()) {
      throw std::runtime_error("optimizer did not find a valid plan");
    }

    auto trace = optimizer.Trace();
    trace.chosen_plan_cost = plan->cost;
    volcano::Exporter::WriteAll(std::filesystem::path(args.at("--out")), optimizer.Graph(), optimizer.Memo(), *plan,
                                trace);

    if (!allow_cp) {
      std::cout << "Volcano transform prototype, not guaranteed complete for cyclic CP-free bushy search\n";
    }
    std::cout << "Best plan: " << plan->ToString() << "\n";
    std::cout << "Cost: " << plan->cost << "\n";
    std::cout << "Exports written to: " << args.at("--out") << "\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "error: " << ex.what() << "\n";
    PrintUsage();
    return 1;
  }
}
