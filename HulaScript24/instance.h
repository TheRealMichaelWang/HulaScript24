#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <optional>
#include <variant>

#include "error.h"
#include "value.h"
#include "instructions.h"

namespace HulaScript::Runtime {
	class instance {
	public:
		instance(uint32_t max_locals, uint32_t max_globals, size_t max_table);
		~instance();

		value make_string(const char* str);
		value make_string(std::string str);

		uint32_t add_constant(value constant);

		uint32_t emit_function_start(std::vector<instruction>& instructions);

		void recycle_function_id(uint32_t func_id) {
			available_function_ids.push(func_id);
		}

		std::vector<instruction>& function_section() {
			return _function_section;
		}

		std::variant<value, error> execute(const std::vector<instruction>& new_instructions);
	private:
		struct table_entry {
			std::map<uint64_t, uint32_t> hash_to_index;
			uint32_t used_elems = 0;

			size_t table_start = 0;
			uint32_t allocated_capacity = 0;
		};

		struct free_table_entry {
			size_t table_start;
			uint32_t allocated_capacity;
		};

		struct loaded_function_entry {
			uint32_t start_address = 0;
			std::set<uint32_t> referenced_func_ids;
			std::set<char*> referenced_const_strs;
			uint32_t length = 0;

			uint32_t parameter_count = 0;
		};

		value* local_elems;
		value* global_elems;
		value* table_elems;

		std::vector<value> evaluation_stack;
		std::vector<value> scratchpad_stack;
		std::vector<uint32_t> return_stack;
		std::vector<uint32_t> extended_offsets;

		std::vector<value> constants;
		std::map<uint64_t, uint32_t> added_constant_hashes;

		uint32_t local_offset, extended_local_offset, global_offset, max_locals, max_globals, start_ip;
		size_t table_offset, max_table;

		std::map<uint64_t, uint32_t> id_global_map;
		std::vector<instruction> _function_section;
		std::map<uint32_t, loaded_function_entry> function_entries;
		std::queue<uint32_t> available_function_ids;
		uint32_t max_function_id;
		
		std::map<uint64_t, table_entry> table_entries;
		std::queue<uint64_t> available_table_ids;
		uint64_t max_table_id;
		std::set<free_table_entry, bool(*)(free_table_entry, free_table_entry)> free_tables;
		std::set<char*> active_strs;

		error type_error(vtype expected, vtype got, uint32_t ip);
		error index_error(double number_index, uint32_t index, uint32_t length, uint32_t ip);

		std::optional<table_entry> allocate_table_no_id(uint32_t element_count);
		std::optional<uint64_t> allocate_table(uint32_t element_count);
		bool reallocate_table(uint64_t table, uint32_t element_count);
		bool reallocate_table(uint64_t table, uint32_t max_elem_extend, uint32_t min_elem_extend);

		void garbage_collect(bool finalize_collect);
	};
}