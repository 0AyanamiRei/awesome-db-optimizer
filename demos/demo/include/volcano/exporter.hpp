#pragma once

#include "volcano/search_strategy.hpp"

#include <filesystem>
#include <vector>

namespace volcano {

class Exporter {
public:
  // Export join graph and best plan as DOT files.
  static void WriteDotFiles(const std::filesystem::path &out_dir,
                            const JoinGraph &graph,
                            const PhysicalPlan &plan);

  // Export a single strategy's trace as JSON.
  static void WriteTrace(const std::filesystem::path &out_dir,
                         const std::string &strategy_name,
                         const SearchTrace &trace);

  // Export comparison of multiple strategy results.
  static void WriteComparison(const std::filesystem::path &out_dir,
                              const std::string &test_name,
                              const std::vector<std::pair<std::string, SearchTrace>> &results);
};

} // namespace volcano
