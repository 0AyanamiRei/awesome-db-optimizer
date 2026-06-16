#pragma once

#include "volcano/optimizer.hpp"

#include <filesystem>

namespace volcano {

class Exporter {
public:
  static void WriteAll(const std::filesystem::path &out_dir, const JoinGraph &graph, const MemoStore &memo,
                       const PhysicalPlan &plan, const TraceCounters &trace);
};

} // namespace volcano
