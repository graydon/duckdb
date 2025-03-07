//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/planner/binder.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/unordered_map.hpp"
#include "duckdb/parser/column_definition.hpp"
#include "duckdb/parser/tokens.hpp"
#include "duckdb/planner/bind_context.hpp"
#include "duckdb/planner/bound_tokens.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/planner/bound_statement.hpp"

namespace duckdb {
class BoundResultModifier;
class ClientContext;
class ExpressionBinder;
class LimitModifier;
class OrderBinder;
class TableCatalogEntry;
class ViewCatalogEntry;

struct CreateInfo;
struct BoundCreateTableInfo;
struct BoundCreateFunctionInfo;
struct CommonTableExpressionInfo;

struct CorrelatedColumnInfo {
	ColumnBinding binding;
	LogicalType type;
	string name;
	idx_t depth;

	explicit CorrelatedColumnInfo(BoundColumnRefExpression &expr)
	    : binding(expr.binding), type(expr.return_type), name(expr.GetName()), depth(expr.depth) {
	}

	bool operator==(const CorrelatedColumnInfo &rhs) const {
		return binding == rhs.binding;
	}
};

//! Bind the parsed query tree to the actual columns present in the catalog.
/*!
  The binder is responsible for binding tables and columns to actual physical
  tables and columns in the catalog. In the process, it also resolves types of
  all expressions.
*/
class Binder : public std::enable_shared_from_this<Binder> {
	friend class ExpressionBinder;
	friend class RecursiveSubqueryPlanner;

public:
	static shared_ptr<Binder> CreateBinder(ClientContext &context, Binder *parent = nullptr, bool inherit_ctes = true);

	//! The client context
	ClientContext &context;
	//! A mapping of names to common table expressions
	unordered_map<string, CommonTableExpressionInfo *> CTE_bindings;
	//! The CTEs that have already been bound
	unordered_set<CommonTableExpressionInfo *> bound_ctes;
	//! The bind context
	BindContext bind_context;
	//! The set of correlated columns bound by this binder (FIXME: this should probably be an unordered_set and not a
	//! vector)
	vector<CorrelatedColumnInfo> correlated_columns;
	//! The set of parameter expressions bound by this binder
	vector<BoundParameterExpression *> *parameters;
	//! Whether or not the bound statement is read-only
	bool read_only;
	//! Whether or not the statement requires a valid transaction to run
	bool requires_valid_transaction;
	//! Whether or not the statement can be streamed to the client
	bool allow_stream_result;
	//! The alias for the currently processing subquery, if it exists
	string alias;
	//! Macro parameter bindings (if any)
	MacroBinding *macro_binding = nullptr;

public:
	BoundStatement Bind(SQLStatement &statement);
	BoundStatement Bind(QueryNode &node);

	unique_ptr<BoundCreateTableInfo> BindCreateTableInfo(unique_ptr<CreateInfo> info);
	void BindCreateViewInfo(CreateViewInfo &base);
	SchemaCatalogEntry *BindSchema(CreateInfo &info);
	SchemaCatalogEntry *BindCreateFunctionInfo(CreateInfo &info);

	//! Check usage, and cast named parameters to their types
	static void BindNamedParameters(unordered_map<string, LogicalType> &types, unordered_map<string, Value> &values,
	                                QueryErrorContext &error_context, string &func_name);

	unique_ptr<BoundTableRef> Bind(TableRef &ref);
	unique_ptr<LogicalOperator> CreatePlan(BoundTableRef &ref);

	//! Generates an unused index for a table
	idx_t GenerateTableIndex();

	//! Add a common table expression to the binder
	void AddCTE(const string &name, CommonTableExpressionInfo *cte);
	//! Find a common table expression by name; returns nullptr if none exists
	CommonTableExpressionInfo *FindCTE(const string &name, bool skip = false);

	bool CTEIsAlreadyBound(CommonTableExpressionInfo *cte);

	void PushExpressionBinder(ExpressionBinder *binder);
	void PopExpressionBinder();
	void SetActiveBinder(ExpressionBinder *binder);
	ExpressionBinder *GetActiveBinder();
	bool HasActiveBinder();

	vector<ExpressionBinder *> &GetActiveBinders();

	void MergeCorrelatedColumns(vector<CorrelatedColumnInfo> &other);
	//! Add a correlated column to this binder (if it does not exist)
	void AddCorrelatedColumn(const CorrelatedColumnInfo &info);

	string FormatError(ParsedExpression &expr_context, const string &message);
	string FormatError(TableRef &ref_context, const string &message);
	string FormatError(idx_t query_location, const string &message);

private:
	//! The parent binder (if any)
	shared_ptr<Binder> parent;
	//! The vector of active binders
	vector<ExpressionBinder *> active_binders;
	//! The count of bound_tables
	idx_t bound_tables;
	//! Whether or not the binder has any unplanned subqueries that still need to be planned
	bool has_unplanned_subqueries = false;
	//! Whether or not subqueries should be planned already
	bool plan_subquery = true;
	//! Whether CTEs should reference the parent binder (if it exists)
	bool inherit_ctes = true;
	//! The root statement of the query that is currently being parsed
	SQLStatement *root_statement = nullptr;

private:
	//! Bind the default values of the columns of a table
	void BindDefaultValues(vector<ColumnDefinition> &columns, vector<unique_ptr<Expression>> &bound_defaults);
	//! Bind a delimiter value (LIMIT or OFFSET)
	unique_ptr<Expression> BindDelimiter(ClientContext &context, unique_ptr<ParsedExpression> delimiter,
	                                     int64_t &delimiter_value);

	//! Move correlated expressions from the child binder to this binder
	void MoveCorrelatedExpressions(Binder &other);

	BoundStatement Bind(SelectStatement &stmt);
	BoundStatement Bind(InsertStatement &stmt);
	BoundStatement Bind(CopyStatement &stmt);
	BoundStatement Bind(DeleteStatement &stmt);
	BoundStatement Bind(UpdateStatement &stmt);
	BoundStatement Bind(CreateStatement &stmt);
	BoundStatement Bind(DropStatement &stmt);
	BoundStatement Bind(AlterStatement &stmt);
	BoundStatement Bind(TransactionStatement &stmt);
	BoundStatement Bind(PragmaStatement &stmt);
	BoundStatement Bind(ExplainStatement &stmt);
	BoundStatement Bind(VacuumStatement &stmt);
	BoundStatement Bind(RelationStatement &stmt);
	BoundStatement Bind(ShowStatement &stmt);
	BoundStatement Bind(CallStatement &stmt);
	BoundStatement Bind(ExportStatement &stmt);
	BoundStatement Bind(SetStatement &stmt);
	BoundStatement Bind(LoadStatement &stmt);

	unique_ptr<BoundQueryNode> BindNode(SelectNode &node);
	unique_ptr<BoundQueryNode> BindNode(SetOperationNode &node);
	unique_ptr<BoundQueryNode> BindNode(RecursiveCTENode &node);
	unique_ptr<BoundQueryNode> BindNode(QueryNode &node);

	unique_ptr<LogicalOperator> VisitQueryNode(BoundQueryNode &node, unique_ptr<LogicalOperator> root);
	unique_ptr<LogicalOperator> CreatePlan(BoundRecursiveCTENode &node);
	unique_ptr<LogicalOperator> CreatePlan(BoundSelectNode &statement);
	unique_ptr<LogicalOperator> CreatePlan(BoundSetOperationNode &node);
	unique_ptr<LogicalOperator> CreatePlan(BoundQueryNode &node);

	unique_ptr<BoundTableRef> Bind(BaseTableRef &ref);
	unique_ptr<BoundTableRef> Bind(CrossProductRef &ref);
	unique_ptr<BoundTableRef> Bind(JoinRef &ref);
	unique_ptr<BoundTableRef> Bind(SubqueryRef &ref, CommonTableExpressionInfo *cte = nullptr);
	unique_ptr<BoundTableRef> Bind(TableFunctionRef &ref);
	unique_ptr<BoundTableRef> Bind(EmptyTableRef &ref);
	unique_ptr<BoundTableRef> Bind(ExpressionListRef &ref);

	bool BindFunctionParameters(vector<unique_ptr<ParsedExpression>> &expressions, vector<LogicalType> &arguments,
	                            vector<Value> &parameters, unordered_map<string, Value> &named_parameters,
	                            unique_ptr<BoundSubqueryRef> &subquery, string &error);

	unique_ptr<LogicalOperator> CreatePlan(BoundBaseTableRef &ref);
	unique_ptr<LogicalOperator> CreatePlan(BoundCrossProductRef &ref);
	unique_ptr<LogicalOperator> CreatePlan(BoundJoinRef &ref);
	unique_ptr<LogicalOperator> CreatePlan(BoundSubqueryRef &ref);
	unique_ptr<LogicalOperator> CreatePlan(BoundTableFunction &ref);
	unique_ptr<LogicalOperator> CreatePlan(BoundEmptyTableRef &ref);
	unique_ptr<LogicalOperator> CreatePlan(BoundExpressionListRef &ref);
	unique_ptr<LogicalOperator> CreatePlan(BoundCTERef &ref);

	unique_ptr<LogicalOperator> BindTable(TableCatalogEntry &table, BaseTableRef &ref);
	unique_ptr<LogicalOperator> BindView(ViewCatalogEntry &view, BaseTableRef &ref);
	unique_ptr<LogicalOperator> BindTableOrView(BaseTableRef &ref);

	BoundStatement BindCopyTo(CopyStatement &stmt);
	BoundStatement BindCopyFrom(CopyStatement &stmt);

	void BindModifiers(OrderBinder &order_binder, QueryNode &statement, BoundQueryNode &result);
	void BindModifierTypes(BoundQueryNode &result, const vector<LogicalType> &sql_types, idx_t projection_index);

	unique_ptr<BoundResultModifier> BindLimit(LimitModifier &limit_mod);
	unique_ptr<Expression> BindFilter(unique_ptr<ParsedExpression> condition);
	unique_ptr<Expression> BindOrderExpression(OrderBinder &order_binder, unique_ptr<ParsedExpression> expr);

	unique_ptr<LogicalOperator> PlanFilter(unique_ptr<Expression> condition, unique_ptr<LogicalOperator> root);

	void PlanSubqueries(unique_ptr<Expression> *expr, unique_ptr<LogicalOperator> *root);
	unique_ptr<Expression> PlanSubquery(BoundSubqueryExpression &expr, unique_ptr<LogicalOperator> &root);

	unique_ptr<LogicalOperator> CastLogicalOperatorToTypes(vector<LogicalType> &source_types,
	                                                       vector<LogicalType> &target_types,
	                                                       unique_ptr<LogicalOperator> op);

	string FindBinding(const string &using_column, const string &join_side);
	bool TryFindBinding(const string &using_column, const string &join_side, string &result);

public:
	// This should really be a private constructor, but make_shared does not allow it...
	Binder(bool I_know_what_I_am_doing, ClientContext &context, shared_ptr<Binder> parent, bool inherit_ctes);
};

} // namespace duckdb
