#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <queue>
#include <set>
#include <array>
#include <string>
#include <optional>

namespace HulaScript {
	class instance {
	public:
		class error {
		public:
			enum etype {
				UNEXPECTED_TYPE,
				INDEX_OUT_OF_RANGE,
				PROPERTY_NOT_FOUND,
				KEY_NOT_FOUND,
				MEMORY,
				NONE
			} type;

			error(error::etype type, std::string message, uint32_t ip);
			error(error::etype type, uint32_t ip);
			error();
		private:
			std::optional<std::string> msg;
			uint32_t ip;
		};

		class value {
		public:
			enum vtype {
				DICTIONARY = 7,
				CLOSURE = 6,
				OBJECT = 5,
				ARRAY = 4,
				FUNC_PTR = 3,
				STRING = 2,
				NUMBER = 1,
				NIL = 0
			} type;

			uint32_t func_id;

			union vdata {
				double number;
				uint64_t table_id;
				char* str;
			} data;

			bool is_gc_type() {
				return type >= vtype::ARRAY;
			}

			bool is_func_type() {
				return type == vtype::FUNC_PTR || type == vtype::CLOSURE;
			}

			uint64_t compute_hash();
		};

		enum opcode {
			//arithmetic op codes
			ADD,
			SUB,
			MUL,
			DIV,
			MOD,
			EXP,

			//variable load/store
			LOAD_LOCAL,
			LOAD_GLOBAL,
			STORE_LOCAL,
			STORE_GLOBAL,
			DECL_LOCAL,
			DECL_GLOBAL,
			UNWIND_LOCALS,

			//table operations
			LOAD_ARRAY_FIXED,
			LOAD_ARRAY_ELEM,
			LOAD_OBJ_PROP,
			LOAD_DICT_ELEM,
			STORE_ARRAY_FIXED,
			STORE_ARRAY_ELEM,
			STORE_OBJ_PROP,
			STORE_DICT_ELEM,
			ALLOCATE_ARRAY,
			ALLOCATE_ARRAY_FIXED,
			ALLOCATE_OBJ,
			ALLOCATE_DICT,

			//control flow
			COND_JUMP_AHEAD,
			JUMP_AHEAD,
			COND_JUMP_BACK,
			JUMP_BACK,

			//function 
			FUNCTION,
			MAKE_CLOSURE,
			CALL_CLOSURE,
			RETURN
		};

		struct instruction {
			opcode op;
			uint32_t operand;
		};

		instance(uint32_t, uint32_t, size_t);
		~instance();

		value make_nil();
		value make_number(double number);
		value make_string(const char* str);
	private:
		struct keymap_entry {
			std::map<uint64_t, uint32_t> hash_to_index;
			int count = 0;
		};

		struct table_entry {
			size_t table_start;
			uint32_t length;
		};

		struct loaded_function_entry {
			uint32_t start_address;
			uint32_t length;
		};

		value* local_elems;
		value* global_elems;
		value* table_elems;

		std::vector<value> evaluation_stack;
		std::vector<uint32_t> return_stack;

		uint32_t local_offset, extended_local_offset, global_offset, max_locals, max_globals;
		size_t table_offset, max_table;

		std::map<uint64_t, uint32_t> id_global_map;
		std::vector<instruction> loaded_functions;
		std::map<uint32_t, loaded_function_entry> function_entries;
		std::queue<uint32_t> available_function_ids;
		uint32_t max_function_id;
		
		std::map<uint64_t, table_entry> table_entries;
		std::queue<uint64_t> available_table_ids;
		uint64_t max_table_id;
		std::set<table_entry, bool(*)(table_entry, table_entry)> free_tables;
		std::set<char*> active_strs;

		std::map<uint64_t, keymap_entry> keymap_entries;

		error last_error;
		std::optional<value> execute(std::vector<instruction> new_instructions);

		error type_error(value::vtype expected, value::vtype got, uint32_t ip);
		error index_error(double number_index, uint32_t index, uint32_t length, uint32_t ip);

		std::optional<table_entry> allocate_table_no_id(uint32_t element_count);
		std::optional<uint64_t> allocate_table(uint32_t element_count);
		bool reallocate_table(uint64_t table, uint32_t element_count);
		bool reallocate_table(uint64_t table, uint32_t max_elem_extend, uint32_t min_elem_extend);

		void garbage_collect();
		void finalize_collect(const std::vector<instruction>& instructions);
	};
}