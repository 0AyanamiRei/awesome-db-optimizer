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
    if (c == '"' || c == '\\') {
      result.push_back('\\');
    }
    result.push_back(c);
  }
  return result;
}

void RequireStream(std::ofstream &out, const std::filesystem::path &path) {
  if (!out) {
    throw std::runtime_error("failed to write export file: " + path.string());
  }
}

void WritePlanNode(std::ofstream &out, const PhysicalPlan &plan, int &next_id, int parent_id = -1) {
  const int current = next_id++;
  out << "  p" << current << " [label=\"" << Escape(PhysicalOpName(plan.op)) << "\\nrows=" << plan.rows
      << "\\ncost=" << plan.cost << "\\n" << Escape(plan.property.ToString()) << "\"];\n";
  if (parent_id >= 0) {
    out << "  p" << parent_id << " -> p" << current << ";\n";
  }
  if (plan.left) {
    WritePlanNode(out, *plan.left, next_id, current);
  }
  if (plan.right) {
    WritePlanNode(out, *plan.right, next_id, current);
  }
  if (plan.child) {
    WritePlanNode(out, *plan.child, next_id, current);
  }
}

} // namespace

void Exporter::WriteAll(const std::filesystem::path &out_dir, const JoinGraph &graph, const MemoStore &memo,
                        const PhysicalPlan &plan, const TraceCounters &trace) {
  std::filesystem::create_directories(out_dir);

  {
    const auto path = out_dir / "join_graph.dot";
    std::ofstream out(path);
    RequireStream(out, path);
    out << "graph join_graph {\n";
    out << "  rankdir=LR;\n";
    for (const auto &relation : graph.Relations()) {
      out << "  r" << relation.id << " [label=\"" << Escape(relation.alias + "\\n" + relation.table_name)
          << "\\nrows=" << relation.rows << "\"];\n";
    }
    for (std::size_t i = 0; i < graph.Predicates().size(); ++i) {
      const auto &predicate = graph.Predicates()[i];
      out << "  r" << graph.RelationByAlias(predicate.left.alias).id << " -- r"
          << graph.RelationByAlias(predicate.right.alias).id << " [label=\""
          << Escape(predicate.left.column + "=" + predicate.right.column) << "\\nsel=" << predicate.selectivity
          << "\"];\n";
    }
    out << "}\n";
  }

  {
    const auto path = out_dir / "memo.dot";
    std::ofstream out(path);
    RequireStream(out, path);
    out << "digraph memo {\n";
    for (std::size_t i = 0; i < memo.Groups().size(); ++i) {
      const auto &group = memo.Groups()[i];
      out << "  g" << i << " [shape=box,label=\"Group " << i << "\\n" << RelSetToString(group.relset, graph)
          << "\"];\n";
      for (std::size_t j = 0; j < group.expressions.size(); ++j) {
        out << "  e" << i << "_" << j << " [label=\"" << Escape(group.expressions[j].ToString(graph)) << "\"];\n";
        out << "  g" << i << " -> e" << i << "_" << j << ";\n";
      }
    }
    out << "}\n";
  }

  {
    const auto path = out_dir / "best_plan.dot";
    std::ofstream out(path);
    RequireStream(out, path);
    out << "digraph best_plan {\n";
    int next_id = 0;
    WritePlanNode(out, plan, next_id);
    out << "}\n";
  }

  {
    const auto path = out_dir / "trace.json";
    std::ofstream out(path);
    RequireStream(out, path);
    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"rule_attempts\": " << trace.rule_attempts << ",\n";
    out << "  \"inserted_expressions\": " << trace.inserted_expressions << ",\n";
    out << "  \"duplicate_expressions\": " << trace.duplicate_expressions << ",\n";
    out << "  \"group_count\": " << trace.group_count << ",\n";
    out << "  \"expression_count\": " << trace.expression_count << ",\n";
    out << "  \"property_cache_hits\": " << trace.property_cache_hits << ",\n";
    out << "  \"chosen_plan_cost\": " << trace.chosen_plan_cost << "\n";
    out << "}\n";
  }
}

} // namespace volcano
