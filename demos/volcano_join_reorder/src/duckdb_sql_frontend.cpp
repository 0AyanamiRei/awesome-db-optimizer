#include "volcano/duckdb_sql_frontend.hpp"

#include "duckdb/common/enums/expression_type.hpp"
#include "duckdb/common/enums/join_type.hpp"
#include "duckdb/common/enums/joinref_type.hpp"
#include "duckdb/common/enums/statement_type.hpp"
#include "duckdb/common/enums/tableref_type.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/comparison_expression.hpp"
#include "duckdb/parser/expression/conjunction_expression.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/parser/tableref/joinref.hpp"

#include <stdexcept>
#include <string>
#include <unordered_set>

namespace volcano {
namespace {

std::string CanonicalPredicateKey(const ColumnRef &left, const ColumnRef &right) {
  auto left_text = left.ToString();
  auto right_text = right.ToString();
  if (right_text < left_text) {
    std::swap(left_text, right_text);
  }
  return left_text + "=" + right_text;
}

ColumnRef ExtractColumnRef(const duckdb::ParsedExpression &expr) {
  if (expr.GetExpressionClass() != duckdb::ExpressionClass::COLUMN_REF) {
    throw std::runtime_error("only qualified column references are supported in join predicates");
  }
  const auto &column = expr.Cast<duckdb::ColumnRefExpression>();
  if (!column.IsQualified()) {
    throw std::runtime_error("join predicate column must be qualified as alias.column: " + column.ToString());
  }
  return ColumnRef{column.GetTableName(), column.GetColumnName()};
}

class GraphExtractor {
public:
  explicit GraphExtractor(const StatsCatalog &stats) : stats_(stats) {
  }

  JoinGraph Extract(const duckdb::SelectNode &select) {
    if (!select.from_table) {
      throw std::runtime_error("SELECT without FROM is not supported");
    }
    if (!select.groups.group_expressions.empty() || select.having || select.qualify) {
      throw std::runtime_error("GROUP BY, HAVING, and QUALIFY are not supported in the join reorder demo");
    }
    for (const auto &expr : select.select_list) {
      if (expr->IsAggregate() || expr->HasSubquery()) {
        throw std::runtime_error("aggregates and subqueries are not supported in SELECT list");
      }
    }

    ExtractTableRef(*select.from_table);
    if (select.where_clause) {
      ExtractPredicate(*select.where_clause);
    }
    return graph_;
  }

private:
  void ExtractTableRef(const duckdb::TableRef &ref) {
    switch (ref.type) {
    case duckdb::TableReferenceType::BASE_TABLE:
      ExtractBaseTable(ref.Cast<duckdb::BaseTableRef>());
      return;
    case duckdb::TableReferenceType::JOIN:
      ExtractJoin(ref.Cast<duckdb::JoinRef>());
      return;
    default:
      throw std::runtime_error("unsupported table reference in FROM clause: " + ref.ToString());
    }
  }

  void ExtractBaseTable(const duckdb::BaseTableRef &ref) {
    const std::string alias = ref.alias.empty() ? ref.table_name : ref.alias;
    const auto &stats = stats_.LookupRelation(alias);
    graph_.AddRelation(alias, ref.table_name, stats.rows, stats.scan_cost);
  }

  void ExtractJoin(const duckdb::JoinRef &join) {
    if (join.type != duckdb::JoinType::INNER) {
      throw std::runtime_error("only INNER joins are supported");
    }
    if (join.ref_type != duckdb::JoinRefType::REGULAR && join.ref_type != duckdb::JoinRefType::CROSS) {
      throw std::runtime_error("only regular INNER JOIN and comma/CROSS join table refs are supported");
    }
    if (!join.left || !join.right) {
      throw std::runtime_error("malformed JOIN table reference");
    }

    ExtractTableRef(*join.left);
    ExtractTableRef(*join.right);
    if (join.condition) {
      ExtractPredicate(*join.condition);
    }
  }

  void ExtractPredicate(const duckdb::ParsedExpression &expr) {
    if (expr.HasSubquery()) {
      throw std::runtime_error("subqueries are not supported in predicates");
    }

    if (expr.GetExpressionClass() == duckdb::ExpressionClass::CONJUNCTION) {
      const auto &conjunction = expr.Cast<duckdb::ConjunctionExpression>();
      if (expr.GetExpressionType() != duckdb::ExpressionType::CONJUNCTION_AND) {
        throw std::runtime_error("OR predicates are not supported");
      }
      for (const auto &child : conjunction.children) {
        ExtractPredicate(*child);
      }
      return;
    }

    if (expr.GetExpressionClass() != duckdb::ExpressionClass::COMPARISON) {
      throw std::runtime_error("only equality join predicates are supported: " + expr.ToString());
    }
    if (expr.GetExpressionType() != duckdb::ExpressionType::COMPARE_EQUAL) {
      throw std::runtime_error("only equality join predicates are supported: " + expr.ToString());
    }

    const auto &comparison = expr.Cast<duckdb::ComparisonExpression>();
    auto left = ExtractColumnRef(*comparison.left);
    auto right = ExtractColumnRef(*comparison.right);
    if (left.alias == right.alias) {
      throw std::runtime_error("single-table predicates are not part of this join reorder demo: " + expr.ToString());
    }

    const auto key = CanonicalPredicateKey(left, right);
    if (seen_predicates_.find(key) != seen_predicates_.end()) {
      return;
    }
    seen_predicates_.insert(key);
    graph_.AddPredicate(left, right, stats_.LookupSelectivity(left, right));
  }

  const StatsCatalog &stats_;
  JoinGraph graph_;
  std::unordered_set<std::string> seen_predicates_;
};

} // namespace

JoinGraph DuckDBSqlFrontend::ParseJoinGraph(const std::string &sql, const StatsCatalog &stats) {
  duckdb::Parser parser;
  parser.ParseQuery(sql);
  if (parser.statements.size() != 1) {
    throw std::runtime_error("expected exactly one SQL statement");
  }

  const auto &statement = *parser.statements.front();
  if (statement.type != duckdb::StatementType::SELECT_STATEMENT) {
    throw std::runtime_error("only SELECT statements are supported");
  }

  const auto &select_statement = statement.Cast<duckdb::SelectStatement>();
  if (!select_statement.node || select_statement.node->type != duckdb::QueryNodeType::SELECT_NODE) {
    throw std::runtime_error("only simple SELECT query nodes are supported");
  }

  const auto &select_node = select_statement.node->Cast<duckdb::SelectNode>();
  return GraphExtractor(stats).Extract(select_node);
}

} // namespace volcano
