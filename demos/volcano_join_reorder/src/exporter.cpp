#include "volcano/exporter.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace volcano {
namespace {

std::string Escape(const std::string &value) {
  std::string result;
  for (const char c : value) {
    if (c == '"' || c == '\\') result.push_back('\\');
    result.push_back(c);
  }
  return result;
}

void RequireStream(std::ofstream &out, const std::filesystem::path &path) {
  if (!out) throw std::runtime_error("failed to write export file: " + path.string());
}

void WritePlanNode(std::ofstream &out, const PhysicalPlan &plan, int &next_id, int parent_id = -1) {
  const int current = next_id++;
  out << "  p" << current << " [label=\"" << Escape(PhysicalOpName(plan.op))
      << "\\nrows=" << plan.rows << "\\ncost=" << plan.cost
      << "\\n" << Escape(plan.property.ToString()) << "\"];\n";
  if (parent_id >= 0) out << "  p" << parent_id << " -> p" << current << ";\n";
  if (plan.left) WritePlanNode(out, *plan.left, next_id, current);
  if (plan.right) WritePlanNode(out, *plan.right, next_id, current);
  if (plan.child) WritePlanNode(out, *plan.child, next_id, current);
}

} // namespace

void Exporter::WriteDotFiles(const std::filesystem::path &out_dir,
                              const JoinGraph &graph,
                              const PhysicalPlan &plan) {
  std::filesystem::create_directories(out_dir);

  // Join graph DOT
  {
    const auto path = out_dir / "join_graph.dot";
    std::ofstream out(path);
    RequireStream(out, path);
    out << "graph join_graph {\n  rankdir=LR;\n";
    for (const auto &relation : graph.Relations()) {
      out << "  r" << relation.id << " [label=\"" << Escape(relation.alias + "\\n" + relation.table_name)
          << "\\nrows=" << relation.rows << "\"];\n";
    }
    for (const auto &predicate : graph.Predicates()) {
      out << "  r" << graph.RelationByAlias(predicate.left.alias).id
          << " -- r" << graph.RelationByAlias(predicate.right.alias).id
          << " [label=\"" << Escape(predicate.left.column + "=" + predicate.right.column)
          << "\\nsel=" << predicate.selectivity << "\"];\n";
    }
    out << "}\n";
  }

  // Best plan DOT
  {
    const auto path = out_dir / "best_plan.dot";
    std::ofstream out(path);
    RequireStream(out, path);
    out << "digraph best_plan {\n";
    int next_id = 0;
    WritePlanNode(out, plan, next_id);
    out << "}\n";
  }
}

void Exporter::WriteTrace(const std::filesystem::path &out_dir,
                           const std::string &strategy_name,
                           const SearchTrace &trace) {
  std::filesystem::create_directories(out_dir);

  const auto filename = strategy_name + "_trace.json";
  // Sanitize: replace parens for filename safety
  std::string safe_name = strategy_name;
  for (auto &c : safe_name) {
    if (c == '(' || c == ')') c = '_';
  }

  const auto path = out_dir / (safe_name + "_trace.json");
  std::ofstream out(path);
  RequireStream(out, path);
  out << std::fixed << std::setprecision(6);
  out << "{\n";
  out << "  \"strategy\": \"" << Escape(strategy_name) << "\",\n";
  out << "  \"partitions_explored\": " << trace.partitions_explored << ",\n";
  out << "  \"partitions_rejected\": " << trace.partitions_rejected << ",\n";
  out << "  \"plans_costed\": " << trace.plans_costed << ",\n";
  out << "  \"cache_hits\": " << trace.cache_hits << ",\n";
  out << "  \"plans_cached\": " << trace.plans_cached << ",\n";
  out << "  \"duplicates_generated\": " << trace.duplicates_generated << ",\n";
  out << "  \"rule_applications\": " << trace.rule_applications << ",\n";
  out << "  \"dp_cells_filled\": " << trace.dp_cells_filled << ",\n";
  out << "  \"branches_pruned\": " << trace.branches_pruned << ",\n";
  out << "  \"best_cost\": " << trace.best_cost << "\n";
  out << "}\n";
}

void Exporter::WriteComparison(const std::filesystem::path &out_dir,
                                const std::string &test_name,
                                const std::vector<std::pair<std::string, SearchTrace>> &results) {
  std::filesystem::create_directories(out_dir);

  const auto path = out_dir / "strategy_comparison.json";
  std::ofstream out(path);
  RequireStream(out, path);
  out << std::fixed << std::setprecision(6);
  out << "{\n";
  out << "  \"query\": \"" << Escape(test_name) << "\",\n";
  out << "  \"strategies\": [\n";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto &[name, trace] = results[i];
    out << "    {\n";
    out << "      \"name\": \"" << Escape(name) << "\",\n";
    out << "      \"partitions_explored\": " << trace.partitions_explored << ",\n";
    out << "      \"partitions_rejected\": " << trace.partitions_rejected << ",\n";
    out << "      \"plans_costed\": " << trace.plans_costed << ",\n";
    out << "      \"cache_hits\": " << trace.cache_hits << ",\n";
    out << "      \"plans_cached\": " << trace.plans_cached << ",\n";
    out << "      \"duplicates_generated\": " << trace.duplicates_generated << ",\n";
    out << "      \"rule_applications\": " << trace.rule_applications << ",\n";
    out << "      \"dp_cells_filled\": " << trace.dp_cells_filled << ",\n";
    out << "      \"branches_pruned\": " << trace.branches_pruned << ",\n";
    out << "      \"best_cost\": " << trace.best_cost << "\n";
    out << "    }";
    if (i + 1 < results.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
}

} // namespace volcano
