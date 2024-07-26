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

namespace HulaScript::Compilation {
	class compiler;
}

namespace HulaScript::Runtime {
	class instance {
	public:
		instance(uint32_t max_locals, uint32_t max_globals, size_t max_table);
		~instance();

		uint32_t make_string(const char* str);
		uint32_t make_string(std::string str);

		std::variant<value, error> execute();
	private:
		enum gc_collection_mode {
			STANDARD = 0,
			FINALIZE_COLLECT_ERROR = 1,
			FINALIZE_COLLECT_RETURN = 2
		};

		struct table_entry {
			std::map<uint64_t, uint32_t> hash_to_index;
			uint32_t used_elems = 0;

			size_t table_start = 0;
			uint32_t allocated_capacity = 0;
		};

		struct gc_block {
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
		std::queue<uint32_t> available_constant_ids;

		uint32_t local_offset, extended_local_offset, global_offset, max_locals, max_globals, start_ip;
		size_t table_offset, max_table;

		std::map<uint64_t, uint32_t> id_global_map;
		std::vector<instruction> loaded_instructions;
		std::queue<uint32_t> available_function_ids;
		uint32_t max_function_id;
		
		std::map<uint32_t, loaded_function_entry> function_entries;
		std::map<uint32_t, Compilation::source_loc> function_src_locs;

		std::map<uint64_t, table_entry> table_entries;
		std::queue<uint64_t> available_table_ids;
		uint64_t max_table_id;
		std::map<uint32_t, gc_block> free_tables;
		std::set<char*> active_strs;

		static error type_error(vtype expected, vtype got, uint32_t ip);
		static error index_error(double number_index, uint32_t index, uint32_t length, uint32_t ip);

		std::optional<gc_block> allocate_block(uint32_t element_count);
		std::optional<uint64_t> allocate_table(uint32_t element_count);
		bool reallocate_table(uint64_t table, uint32_t element_count);
		bool reallocate_table(uint64_t table, uint32_t max_elem_extend, uint32_t min_elem_extend);

		void garbage_collect(gc_collection_mode mode);

		uint32_t emit_function_start(std::vector<instruction>& instructions);
		uint32_t add_constant(value constant);

		friend class HulaScript::Compilation::compiler;
	};
}