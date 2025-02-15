#pragma once

#include <cstdlib>
#include <cstdint>
#include <utility>

namespace HulaScript::Runtime {
	enum vtype {
		CLOSURE = 4,
		TABLE = 3,
		STRING = 2,
		NUMBER = 1,
		NIL = 0,

		FOREIGN_RESOURCE = 5,
		INTERNAL_CONSTHASH = 7
	};

	struct value {
	public:
		value() : _type(vtype::NIL), func_id(0), data({ .str = NULL }) {}
		value(double number) : _type(vtype::NUMBER), func_id(0), data({.number = number}) { }
		value(bool b) : _type(vtype::NUMBER), func_id(0), data({.number = b ? 1.0 : 0.0}) { }
		value(char* raw_cstr) : _type(vtype::STRING), func_id(0), data({.str = raw_cstr}) { }
		value(uint64_t raw_table_id) : _type(vtype::TABLE), func_id(0), data({.table_id = raw_table_id}) { }
		value(uint32_t raw_func_id, uint64_t raw_table_id) : _type(vtype::CLOSURE), func_id(raw_func_id), data({.table_id = raw_table_id}) { }

		value(vtype type, uint64_t raw_data) : _type(type), func_id(0), data({.table_id = raw_data}){ }
		value(vtype type, void* raw_id) : _type(type), func_id(0), data({ .ptr = raw_id }) { }

		constexpr vtype type() const {
			return _type;
		}

		constexpr double number() const {
			return data.number;
		}

		constexpr char* str() const {
			return data.str;
		}

		constexpr uint64_t table_id() const {
			return data.table_id;
		}

		constexpr std::pair<uint32_t, uint64_t> closure() const {
			return std::make_pair(func_id, data.table_id);
		}

		constexpr void* raw_ptr() const {
			return data.ptr;
		}

		//computes a unique value hash
		const uint64_t compute_hash() const;

		//computes a hash specifically to represent value keys
		const uint64_t compute_key_hash() const;

		//std::string to_print_string();
	private:
		vtype _type;

		uint32_t func_id;

		union vdata {
			double number;
			uint64_t table_id;
			char* str;
			void* ptr;
		} data;
	};
}