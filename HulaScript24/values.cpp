#include <cstring>
#include <cassert>
#include <string>
#include <sstream>
#include "hash.h"
#include "value.h"
#include "instance.h"

using namespace HulaScript::Runtime;

const uint64_t value::compute_hash() const {
	uint64_t init_hash = 0;
	switch (_type)
	{
	case vtype::CLOSURE:
		init_hash = hash_combine(func_id, data.table_id);
		break;
	case vtype::FOREIGN_RESOURCE:
		[[fallthrough]];
	case vtype::INTERNAL_CONSTHASH:
		[[fallthrough]];
	case vtype::NUMBER:
		[[fallthrough]];
	case vtype::TABLE:
		init_hash = data.table_id;
		break;
	case vtype::STRING:
		init_hash = str_hash(data.str);
		break;
	case vtype::NIL:
		return 0;
	}

	return hash_combine(init_hash, (uint64_t)_type);
}

const uint64_t value::compute_key_hash() const {
	switch (_type)
	{
	case vtype::INTERNAL_CONSTHASH:
		return data.table_id;
	default:
		return compute_hash();
	}
}

std::string instance::value_to_print_str(value& val) const {
	switch (val.type())
	{
	case HulaScript::Runtime::CLOSURE: {
		auto closure = val.closure();

		std::stringstream ss;
		ss << "closure(func_id=" << closure.first << ", capture_table=";

		table_entry& entry = table_entries.unsafe_get(closure.second);
		ss << '[';
		for (uint_fast32_t i = 0; i < entry.used_elems; i++) {
			if (i > 0) {
				ss << ", ";
			}
			ss << value_to_print_str(table_elems[entry.block.table_start + i]);
		}
		ss << ']';
		return ss.str();
	}
	case HulaScript::Runtime::TABLE: {
		table_entry& entry = table_entries.unsafe_get(val.table_id());

		std::stringstream ss;
		ss << '[';
		for (uint_fast32_t i = 0; i < entry.used_elems; i++) {
			if (i > 0) {
				ss << ", ";
			}
			ss << value_to_print_str(table_elems[entry.block.table_start + i]);
		}
		ss << ']';
		return ss.str();
	}
	case HulaScript::Runtime::STRING:
		return std::string(val.str());
	case HulaScript::Runtime::NUMBER:
		return std::to_string(val.number());
	case HulaScript::Runtime::NIL:
		return "nil";
	default:
		return "error";
	}
}