#include "duckdb_python/array_wrapper.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/hugeint.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "utf8proc_wrapper.hpp"
#include "duckdb/common/types/interval.hpp"

namespace duckdb {

namespace duckdb_py_convert {

struct RegularConvert {
	template <class DUCKDB_T, class NUMPY_T>
	static NUMPY_T ConvertValue(DUCKDB_T val) {
		return (NUMPY_T)val;
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return 0;
	}
};

struct TimestampConvert {
	template <class DUCKDB_T, class NUMPY_T>
	static int64_t ConvertValue(timestamp_t val) {
		return Timestamp::GetEpochNanoSeconds(val);
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return 0;
	}
};

struct TimestampConvertSec {
	template <class DUCKDB_T, class NUMPY_T>
	static int64_t ConvertValue(timestamp_t val) {
		return Timestamp::GetEpochNanoSeconds(Timestamp::FromEpochSeconds(val.value));
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return 0;
	}
};

struct TimestampConvertMilli {
	template <class DUCKDB_T, class NUMPY_T>
	static int64_t ConvertValue(timestamp_t val) {
		return Timestamp::GetEpochNanoSeconds(Timestamp::FromEpochMs(val.value));
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return 0;
	}
};

struct TimestampConvertNano {
	template <class DUCKDB_T, class NUMPY_T>
	static int64_t ConvertValue(timestamp_t val) {
		return val.value;
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return 0;
	}
};

struct DateConvert {
	template <class DUCKDB_T, class NUMPY_T>
	static int64_t ConvertValue(date_t val) {
		return Date::EpochNanoseconds(val);
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return 0;
	}
};

struct IntervalConvert {
	template <class DUCKDB_T, class NUMPY_T>
	static int64_t ConvertValue(interval_t val) {
		return Interval::GetMilli(val);
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return 0;
	}
};

struct TimeConvert {
	template <class DUCKDB_T, class NUMPY_T>
	static PyObject *ConvertValue(dtime_t val) {
		auto str = duckdb::Time::ToString(val);
		return PyUnicode_FromStringAndSize(str.c_str(), str.size());
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return nullptr;
	}
};

struct StringConvert {
#if PY_MAJOR_VERSION >= 3
	template <class T>
	static void ConvertUnicodeValueTemplated(T *result, int32_t *codepoints, idx_t codepoint_count, const char *data,
	                                         idx_t ascii_count) {
		// we first fill in the batch of ascii characters directly
		for (idx_t i = 0; i < ascii_count; i++) {
			result[i] = data[i];
		}
		// then we fill in the remaining codepoints from our codepoint array
		for (idx_t i = 0; i < codepoint_count; i++) {
			result[ascii_count + i] = codepoints[i];
		}
	}

	static PyObject *ConvertUnicodeValue(const char *data, idx_t len, idx_t start_pos) {
		// slow path: check the code points
		// we know that all characters before "start_pos" were ascii characters, so we don't need to check those

		// allocate an array of code points so we only have to convert the codepoints once
		// short-string optimization
		// we know that the max amount of codepoints is the length of the string
		// for short strings (less than 64 bytes) we simply statically allocate an array of 256 bytes (64x int32)
		// this avoids memory allocation for small strings (common case)
		idx_t remaining = len - start_pos;
		unique_ptr<int32_t[]> allocated_codepoints;
		int32_t static_codepoints[64];
		int32_t *codepoints;
		if (remaining > 64) {
			allocated_codepoints = unique_ptr<int32_t[]>(new int32_t[remaining]);
			codepoints = allocated_codepoints.get();
		} else {
			codepoints = static_codepoints;
		}
		// now we iterate over the remainder of the string to convert the UTF8 string into a sequence of codepoints
		// and to find the maximum codepoint
		int32_t max_codepoint = 127;
		int sz;
		idx_t pos = start_pos;
		idx_t codepoint_count = 0;
		while (pos < len) {
			codepoints[codepoint_count] = Utf8Proc::UTF8ToCodepoint(data + pos, sz);
			pos += sz;
			if (codepoints[codepoint_count] > max_codepoint) {
				max_codepoint = codepoints[codepoint_count];
			}
			codepoint_count++;
		}
		// based on the max codepoint, we construct the result string
		auto result = PyUnicode_New(start_pos + codepoint_count, max_codepoint);
		// based on the resulting unicode kind, we fill in the code points
		auto kind = PyUnicode_KIND(result);
		switch (kind) {
		case PyUnicode_1BYTE_KIND:
			ConvertUnicodeValueTemplated<Py_UCS1>(PyUnicode_1BYTE_DATA(result), codepoints, codepoint_count, data,
			                                      start_pos);
			break;
		case PyUnicode_2BYTE_KIND:
			ConvertUnicodeValueTemplated<Py_UCS2>(PyUnicode_2BYTE_DATA(result), codepoints, codepoint_count, data,
			                                      start_pos);
			break;
		case PyUnicode_4BYTE_KIND:
			ConvertUnicodeValueTemplated<Py_UCS4>(PyUnicode_4BYTE_DATA(result), codepoints, codepoint_count, data,
			                                      start_pos);
			break;
		default:
			throw std::runtime_error("Unsupported typekind for Python Unicode Compact decode");
		}
		return result;
	}

	template <class DUCKDB_T, class NUMPY_T>
	static PyObject *ConvertValue(string_t val) {
		// we could use PyUnicode_FromStringAndSize here, but it does a lot of verification that we don't need
		// because of that it is a lot slower than it needs to be
		auto data = (uint8_t *)val.GetDataUnsafe();
		auto len = val.GetSize();
		// check if there are any non-ascii characters in there
		for (idx_t i = 0; i < len; i++) {
			if (data[i] > 127) {
				// there are! fallback to slower case
				return ConvertUnicodeValue((const char *)data, len, i);
			}
		}
		// no unicode: fast path
		// directly construct the string and memcpy it
		auto result = PyUnicode_New(len, 127);
		auto target_data = PyUnicode_DATA(result);
		memcpy(target_data, data, len);
		return result;
	}
#else
	template <class DUCKDB_T, class NUMPY_T>
	static PyObject *ConvertValue(string_t val) {
		return py::str(val.GetString()).release().ptr();
	}
#endif

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return nullptr;
	}
};

struct BlobConvert {
	template <class DUCKDB_T, class NUMPY_T>
	static PyObject *ConvertValue(string_t val) {
		return PyByteArray_FromStringAndSize(val.GetDataUnsafe(), val.GetSize());
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return nullptr;
	}
};

struct IntegralConvert {
	template <class DUCKDB_T, class NUMPY_T>
	static NUMPY_T ConvertValue(DUCKDB_T val) {
		return NUMPY_T(val);
	}

	template <class NUMPY_T>
	static NUMPY_T NullValue() {
		return 0;
	}
};

template <>
double IntegralConvert::ConvertValue(hugeint_t val) {
	double result;
	Hugeint::TryCast(val, result);
	return result;
}

} // namespace duckdb_py_convert

template <class DUCKDB_T, class NUMPY_T, class CONVERT>
static bool ConvertColumn(idx_t target_offset, data_ptr_t target_data, bool *target_mask, VectorData &idata,
                          idx_t count) {
	auto src_ptr = (DUCKDB_T *)idata.data;
	auto out_ptr = (NUMPY_T *)target_data;
	if (!idata.validity.AllValid()) {
		for (idx_t i = 0; i < count; i++) {
			idx_t src_idx = idata.sel->get_index(i);
			idx_t offset = target_offset + i;
			if (!idata.validity.RowIsValidUnsafe(src_idx)) {
				target_mask[offset] = true;
				out_ptr[offset] = CONVERT::template NullValue<NUMPY_T>();
			} else {
				out_ptr[offset] = CONVERT::template ConvertValue<DUCKDB_T, NUMPY_T>(src_ptr[src_idx]);
				target_mask[offset] = false;
			}
		}
		return true;
	} else {
		for (idx_t i = 0; i < count; i++) {
			idx_t src_idx = idata.sel->get_index(i);
			idx_t offset = target_offset + i;
			out_ptr[offset] = CONVERT::template ConvertValue<DUCKDB_T, NUMPY_T>(src_ptr[src_idx]);
			target_mask[offset] = false;
		}
		return false;
	}
}

template <class T>
static bool ConvertColumnRegular(idx_t target_offset, data_ptr_t target_data, bool *target_mask, VectorData &idata,
                                 idx_t count) {
	return ConvertColumn<T, T, duckdb_py_convert::RegularConvert>(target_offset, target_data, target_mask, idata,
	                                                              count);
}

template <class DUCKDB_T>
static bool ConvertDecimalInternal(idx_t target_offset, data_ptr_t target_data, bool *target_mask, VectorData &idata,
                                   idx_t count, double division) {
	auto src_ptr = (DUCKDB_T *)idata.data;
	auto out_ptr = (double *)target_data;
	if (!idata.validity.AllValid()) {
		for (idx_t i = 0; i < count; i++) {
			idx_t src_idx = idata.sel->get_index(i);
			idx_t offset = target_offset + i;
			if (!idata.validity.RowIsValidUnsafe(src_idx)) {
				target_mask[offset] = true;
			} else {
				out_ptr[offset] =
				    duckdb_py_convert::IntegralConvert::ConvertValue<DUCKDB_T, double>(src_ptr[src_idx]) / division;
				target_mask[offset] = false;
			}
		}
		return true;
	} else {
		for (idx_t i = 0; i < count; i++) {
			idx_t src_idx = idata.sel->get_index(i);
			idx_t offset = target_offset + i;
			out_ptr[offset] =
			    duckdb_py_convert::IntegralConvert::ConvertValue<DUCKDB_T, double>(src_ptr[src_idx]) / division;
			target_mask[offset] = false;
		}
		return false;
	}
}

static bool ConvertDecimal(const LogicalType &decimal_type, idx_t target_offset, data_ptr_t target_data,
                           bool *target_mask, VectorData &idata, idx_t count) {
	auto dec_scale = DecimalType::GetScale(decimal_type);
	double division = pow(10, dec_scale);
	switch (decimal_type.InternalType()) {
	case PhysicalType::INT16:
		return ConvertDecimalInternal<int16_t>(target_offset, target_data, target_mask, idata, count, division);
	case PhysicalType::INT32:
		return ConvertDecimalInternal<int32_t>(target_offset, target_data, target_mask, idata, count, division);
	case PhysicalType::INT64:
		return ConvertDecimalInternal<int64_t>(target_offset, target_data, target_mask, idata, count, division);
	case PhysicalType::INT128:
		return ConvertDecimalInternal<hugeint_t>(target_offset, target_data, target_mask, idata, count, division);
	default:
		throw NotImplementedException("Unimplemented internal type for DECIMAL");
	}
}

RawArrayWrapper::RawArrayWrapper(const LogicalType &type) : data(nullptr), type(type), count(0) {
	switch (type.id()) {
	case LogicalTypeId::BOOLEAN:
		type_width = sizeof(bool);
		break;
	case LogicalTypeId::UTINYINT:
		type_width = sizeof(uint8_t);
		break;
	case LogicalTypeId::USMALLINT:
		type_width = sizeof(uint16_t);
		break;
	case LogicalTypeId::UINTEGER:
		type_width = sizeof(uint32_t);
		break;
	case LogicalTypeId::UBIGINT:
		type_width = sizeof(uint64_t);
		break;
	case LogicalTypeId::TINYINT:
		type_width = sizeof(int8_t);
		break;
	case LogicalTypeId::SMALLINT:
		type_width = sizeof(int16_t);
		break;
	case LogicalTypeId::INTEGER:
		type_width = sizeof(int32_t);
		break;
	case LogicalTypeId::BIGINT:
		type_width = sizeof(int64_t);
		break;
	case LogicalTypeId::HUGEINT:
		type_width = sizeof(double);
		break;
	case LogicalTypeId::FLOAT:
		type_width = sizeof(float);
		break;
	case LogicalTypeId::DOUBLE:
		type_width = sizeof(double);
		break;
	case LogicalTypeId::DECIMAL:
		type_width = sizeof(double);
		break;
	case LogicalTypeId::TIMESTAMP:
	case LogicalTypeId::TIMESTAMP_SEC:
	case LogicalTypeId::TIMESTAMP_MS:
	case LogicalTypeId::TIMESTAMP_NS:
	case LogicalTypeId::DATE:
	case LogicalTypeId::INTERVAL:
		type_width = sizeof(int64_t);
		break;
	case LogicalTypeId::TIME:
		type_width = sizeof(PyObject *);
		break;
	case LogicalTypeId::VARCHAR:
		type_width = sizeof(PyObject *);
		break;
	case LogicalTypeId::BLOB:
		type_width = sizeof(PyObject *);
		break;
	default:
		throw std::runtime_error("Unsupported type " + type.ToString() + " for DuckDB -> NumPy conversion");
	}
}

void RawArrayWrapper::Initialize(idx_t capacity) {
	string dtype;
	switch (type.id()) {
	case LogicalTypeId::BOOLEAN:
		dtype = "bool";
		break;
	case LogicalTypeId::TINYINT:
		dtype = "int8";
		break;
	case LogicalTypeId::SMALLINT:
		dtype = "int16";
		break;
	case LogicalTypeId::INTEGER:
		dtype = "int32";
		break;
	case LogicalTypeId::BIGINT:
		dtype = "int64";
		break;
	case LogicalTypeId::UTINYINT:
		dtype = "uint8";
		break;
	case LogicalTypeId::USMALLINT:
		dtype = "uint16";
		break;
	case LogicalTypeId::UINTEGER:
		dtype = "uint32";
		break;
	case LogicalTypeId::UBIGINT:
		dtype = "uint64";
		break;
	case LogicalTypeId::FLOAT:
		dtype = "float32";
		break;
	case LogicalTypeId::HUGEINT:
	case LogicalTypeId::DOUBLE:
	case LogicalTypeId::DECIMAL:
		dtype = "float64";
		break;
	case LogicalTypeId::TIMESTAMP:
	case LogicalTypeId::TIMESTAMP_NS:
	case LogicalTypeId::TIMESTAMP_MS:
	case LogicalTypeId::TIMESTAMP_SEC:
	case LogicalTypeId::DATE:
		dtype = "datetime64[ns]";
		break;
	case LogicalTypeId::INTERVAL:
		dtype = "timedelta64[ns]";
		break;
	case LogicalTypeId::TIME:
	case LogicalTypeId::VARCHAR:
	case LogicalTypeId::BLOB:
		dtype = "object";
		break;
	default:
		throw std::runtime_error("unsupported type " + type.ToString());
	}
	array = py::array(py::dtype(dtype), capacity);
	data = (data_ptr_t)array.mutable_data();
}

void RawArrayWrapper::Resize(idx_t new_capacity) {
	vector<ssize_t> new_shape {ssize_t(new_capacity)};
	array.resize(new_shape, false);
	data = (data_ptr_t)array.mutable_data();
}

ArrayWrapper::ArrayWrapper(const LogicalType &type) : requires_mask(false) {
	data = make_unique<RawArrayWrapper>(type);
	mask = make_unique<RawArrayWrapper>(LogicalType::BOOLEAN);
}

void ArrayWrapper::Initialize(idx_t capacity) {
	data->Initialize(capacity);
	mask->Initialize(capacity);
}

void ArrayWrapper::Resize(idx_t new_capacity) {
	data->Resize(new_capacity);
	mask->Resize(new_capacity);
}

void ArrayWrapper::Append(idx_t current_offset, Vector &input, idx_t count) {
	auto dataptr = data->data;
	auto maskptr = (bool *)mask->data;
	D_ASSERT(dataptr);
	D_ASSERT(maskptr);
	D_ASSERT(input.GetType() == data->type);
	bool may_have_null;

	VectorData idata;
	input.Orrify(count, idata);
	switch (input.GetType().id()) {
	case LogicalTypeId::BOOLEAN:
		may_have_null = ConvertColumnRegular<bool>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::TINYINT:
		may_have_null = ConvertColumnRegular<int8_t>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::SMALLINT:
		may_have_null = ConvertColumnRegular<int16_t>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::INTEGER:
		may_have_null = ConvertColumnRegular<int32_t>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::BIGINT:
		may_have_null = ConvertColumnRegular<int64_t>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::UTINYINT:
		may_have_null = ConvertColumnRegular<uint8_t>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::USMALLINT:
		may_have_null = ConvertColumnRegular<uint16_t>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::UINTEGER:
		may_have_null = ConvertColumnRegular<uint32_t>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::UBIGINT:
		may_have_null = ConvertColumnRegular<uint64_t>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::HUGEINT:
		may_have_null = ConvertColumn<hugeint_t, double, duckdb_py_convert::IntegralConvert>(current_offset, dataptr,
		                                                                                     maskptr, idata, count);
		break;
	case LogicalTypeId::FLOAT:
		may_have_null = ConvertColumnRegular<float>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::DOUBLE:
		may_have_null = ConvertColumnRegular<double>(current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::DECIMAL:
		may_have_null = ConvertDecimal(input.GetType(), current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::TIMESTAMP:
		may_have_null = ConvertColumn<timestamp_t, int64_t, duckdb_py_convert::TimestampConvert>(
		    current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::TIMESTAMP_SEC:
		may_have_null = ConvertColumn<timestamp_t, int64_t, duckdb_py_convert::TimestampConvertSec>(
		    current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::TIMESTAMP_MS:
		may_have_null = ConvertColumn<timestamp_t, int64_t, duckdb_py_convert::TimestampConvertMilli>(
		    current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::TIMESTAMP_NS:
		may_have_null = ConvertColumn<timestamp_t, int64_t, duckdb_py_convert::TimestampConvertNano>(
		    current_offset, dataptr, maskptr, idata, count);
		break;
	case LogicalTypeId::DATE:
		may_have_null = ConvertColumn<date_t, int64_t, duckdb_py_convert::DateConvert>(current_offset, dataptr, maskptr,
		                                                                               idata, count);
		break;
	case LogicalTypeId::TIME:
		may_have_null = ConvertColumn<dtime_t, PyObject *, duckdb_py_convert::TimeConvert>(current_offset, dataptr,
		                                                                                   maskptr, idata, count);
		break;
	case LogicalTypeId::INTERVAL:
		may_have_null = ConvertColumn<interval_t, int64_t, duckdb_py_convert::IntervalConvert>(current_offset, dataptr,
		                                                                                       maskptr, idata, count);
		break;
	case LogicalTypeId::VARCHAR:
		may_have_null = ConvertColumn<string_t, PyObject *, duckdb_py_convert::StringConvert>(current_offset, dataptr,
		                                                                                      maskptr, idata, count);
		break;
	case LogicalTypeId::BLOB:
		may_have_null = ConvertColumn<string_t, PyObject *, duckdb_py_convert::BlobConvert>(current_offset, dataptr,
		                                                                                    maskptr, idata, count);
		break;
	default:
		throw std::runtime_error("unsupported type " + input.GetType().ToString());
	}
	if (may_have_null) {
		requires_mask = true;
	}
	data->count += count;
	mask->count += count;
}

py::object ArrayWrapper::ToArray(idx_t count) const {
	D_ASSERT(data->array && mask->array);
	data->Resize(data->count);
	if (!requires_mask) {
		return move(data->array);
	}
	mask->Resize(mask->count);
	// construct numpy arrays from the data and the mask
	auto values = move(data->array);
	auto nullmask = move(mask->array);

	// create masked array and return it
	auto masked_array = py::module::import("numpy.ma").attr("masked_array")(values, nullmask);
	return masked_array;
}

NumpyResultConversion::NumpyResultConversion(vector<LogicalType> &types, idx_t initial_capacity)
    : count(0), capacity(0) {
	owned_data.reserve(types.size());
	for (auto &type : types) {
		owned_data.emplace_back(type);
	}
	Resize(initial_capacity);
}

void NumpyResultConversion::Resize(idx_t new_capacity) {
	if (capacity == 0) {
		for (auto &data : owned_data) {
			data.Initialize(new_capacity);
		}
	} else {
		for (auto &data : owned_data) {
			data.Resize(new_capacity);
		}
	}
	capacity = new_capacity;
}

void NumpyResultConversion::Append(DataChunk &chunk) {
	if (count + chunk.size() > capacity) {
		Resize(capacity * 2);
	}
	for (idx_t col_idx = 0; col_idx < owned_data.size(); col_idx++) {
		owned_data[col_idx].Append(count, chunk.data[col_idx], chunk.size());
	}
	count += chunk.size();
#ifdef DEBUG
	for (auto &data : owned_data) {
		D_ASSERT(data.data->count == count);
		D_ASSERT(data.mask->count == count);
	}
#endif
}

} // namespace duckdb