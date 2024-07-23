#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <queue>
#include <set>
#include <array>
#include <string>
#include <optional>
#include "compiler.h"

namespace HulaScript {
	class instance {
	public:
		class error {
		public:
			enum etype {
				UNEXPECTED_TYPE,
				ARGUMENT_COUNT_MISMATCH,
				MEMORY,
				INTERNAL_ERROR
			} type;

			error(error::etype type, std::string message, uint32_t ip);
			error(error::etype type, uint32_t ip);
		private:
			std::optional<std::string> msg;
			uint32_t ip;
		};

		struct value {
			enum vtype {
				CLOSURE = 4,
				TABLE = 3,
				STRING = 2,
				NUMBER = 1,
				NIL = 0
			} type = vtype::NIL;

			uint32_t func_id;

			union vdata {
				double number;
				uint64_t table_id;
				char* str;
			} data;

			const bool is_gc_type() {
				return type >= vtype::TABLE;
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

			//comparison operators
			LESS,
			MORE,
			LESS_EQUAL,
			MORE_EQUAL,
			EQUALS,
			NOT_EQUALS,

			//logical operators,
			AND,
			OR,

			//unary operators
			NEGATE,
			NOT,

			//variable load/store
			LOAD_LOCAL,
			LOAD_GLOBAL,
			STORE_LOCAL,
			STORE_GLOBAL,
			DECL_LOCAL,
			DECL_GLOBAL,
			UNWIND_LOCALS,

			//other miscellaneous operations
			LOAD_CONSTANT,
			PUSH_NIL,
			DISCARD_TOP,
			PUSH_SCRATCHPAD,
			POP_SCRATCHPAD,
			REVERSE_SCRATCHPAD,
			DUPLICATE,

			//table operations
			LOAD_TABLE_ELEM,
			STORE_TABLE_ELEM,
			ALLOCATE_DYN,
			ALLOCATE_FIXED,
			ALLOCATE_LITERAL,

			//control flow
			COND_JUMP_AHEAD,
			JUMP_AHEAD,
			COND_JUMP_BACK,
			JUMP_BACK,

			//function 
			FUNCTION,
			FUNCTION_END,
			MAKE_CLOSURE,
			CHECK_ARGS,
			CALL,
			RETURN,

			//invalid
			INVALID
		};

		struct instruction {
			opcode op;
			uint32_t operand;
		};

		instance(uint32_t, uint32_t, size_t);
		~instance();

		std::variant<value, error, compiler::error> run(std::string code, std::optional<std::string> source);

		value make_nil();
		value make_number(double number);
		value make_string(const char* str);
		value make_string(std::string str);
		value make_bool(bool b);

		uint32_t add_constant(value constant);

		uint32_t emit_function_start(std::vector<instruction>& instructions);

		void recycle_function_id(uint32_t func_id) {
			available_function_ids.push(func_id);
		}
	private:
		struct table_entry {
			std::map<uint64_t, uint32_t> hash_to_index;
			uint32_t used_elems;

			size_t table_start;
			uint32_t allocated_capacity;
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

		compiler my_compiler;

		value* local_elems;
		value* global_elems;
		value* table_elems;

		std::vector<value> evaluation_stack;
		std::vector<value> scratchpad_stack;
		std::vector<uint32_t> return_stack;
		std::vector<uint32_t> extended_offsets;

		std::vector<value> constants;
		std::map<uint64_t, uint32_t> added_constant_hashes;

		uint32_t local_offset, extended_local_offset, global_offset, max_locals, max_globals;
		size_t table_offset, max_table;

		std::map<uint64_t, uint32_t> id_global_map;
		std::vector<instruction> function_section;
		std::map<uint32_t, loaded_function_entry> function_entries;
		std::queue<uint32_t> available_function_ids;
		uint32_t max_function_id;
		
		std::map<uint64_t, table_entry> table_entries;
		std::queue<uint64_t> available_table_ids;
		uint64_t max_table_id;
		std::set<free_table_entry, bool(*)(free_table_entry, free_table_entry)> free_tables;
		std::set<char*> active_strs;

		std::variant<value, error> execute(const uint32_t start_ip, const std::vector<instruction>& new_instructions);

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