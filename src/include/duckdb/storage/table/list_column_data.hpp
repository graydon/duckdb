//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/storage/table/list_column_data.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/storage/table/column_data.hpp"
#include "duckdb/storage/table/validity_column_data.hpp"

namespace duckdb {

//! List column data represents a list
class ListColumnData : public ColumnData {
public:
	ListColumnData(DataTableInfo &info, idx_t column_index, idx_t start_row, LogicalType type,
	               ColumnData *parent = nullptr);

	//! The child-column of the list
	unique_ptr<ColumnData> child_column;
	//! The validity column data of the struct
	ValidityColumnData validity;

public:
	bool CheckZonemap(ColumnScanState &state, TableFilter &filter) override;

	void InitializeScan(ColumnScanState &state) override;
	void InitializeScanWithOffset(ColumnScanState &state, idx_t row_idx) override;

	void Scan(Transaction &transaction, idx_t vector_index, ColumnScanState &state, Vector &result) override;
	void ScanCommitted(idx_t vector_index, ColumnScanState &state, Vector &result, bool allow_updates) override;
	void ScanCount(ColumnScanState &state, Vector &result, idx_t count) override;

	void Skip(ColumnScanState &state, idx_t count = STANDARD_VECTOR_SIZE) override;

	void InitializeAppend(ColumnAppendState &state) override;
	void Append(BaseStatistics &stats, ColumnAppendState &state, Vector &vector, idx_t count) override;
	void RevertAppend(row_t start_row) override;
	void Fetch(ColumnScanState &state, row_t row_id, Vector &result) override;
	void FetchRow(Transaction &transaction, ColumnFetchState &state, row_t row_id, Vector &result,
	              idx_t result_idx) override;
	void Update(Transaction &transaction, idx_t column_index, Vector &update_vector, row_t *row_ids, idx_t offset,
	            idx_t update_count) override;
	void UpdateColumn(Transaction &transaction, const vector<column_t> &column_path, Vector &update_vector,
	                  row_t *row_ids, idx_t update_count, idx_t depth) override;
	unique_ptr<BaseStatistics> GetUpdateStatistics() override;

	void CommitDropColumn() override;
	void Initialize(PersistentColumnData &column_data) override;

	unique_ptr<ColumnCheckpointState> CreateCheckpointState(RowGroup &row_group, TableDataWriter &writer) override;
	unique_ptr<ColumnCheckpointState> Checkpoint(RowGroup &row_group, TableDataWriter &writer) override;

	void DeserializeColumn(Deserializer &source) override;

	void GetStorageInfo(idx_t row_group_index, vector<idx_t> col_path, vector<vector<Value>> &result) override;

private:
	list_entry_t FetchListEntry(idx_t row_idx);
};

} // namespace duckdb
