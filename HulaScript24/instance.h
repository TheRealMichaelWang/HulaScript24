#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include <optional>
#include <variant>

#include "sparsepp/spp.h"

#include "error.h"
#include "value.h"
#include "instructions.h"
#include "ffi.h"
#include "hash.h"

namespace HulaScript::Compilation {
	class compiler;
}

namespace HulaScript::Runtime {
	class instance {
	public:
		instance(uint32_t max_locals, uint32_t max_globals, size_t max_table);
		~instance();

		std::variant<value, error> execute();

		std::string value_to_print_str(value& val);
	private:
		enum gc_collection_mode {
			STANDARD = 0,
			FINALIZE_COLLECT_ERROR = 1,
			FINALIZE_COLLECT_RETURN = 2
		};

		struct gc_block {
			size_t table_start;
			uint32_t allocated_capacity;
		};

		struct table_entry {
			std::pair<uint64_t, uint32_t>* key_hashes;
			uint32_t key_hash_capacity;
			uint32_t used_elems = 0;
			
			gc_block block;
		};

		struct loaded_function_entry {
			uint32_t start_address = 0;
			std::vector<uint32_t> referenced_func_ids;
			std::vector<char*> referenced_const_strs;
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

		uint32_t local_offset, extended_local_offset, global_offset, max_locals, max_globals, start_ip;
		size_t table_offset, max_table;

		std::vector<instruction> loaded_instructions;
		std::map<uint32_t, source_loc> ip_src_locs;
		uint32_t next_function_id, top_level_local_offset;
		
		spp::sparsetable<loaded_function_entry, SPP_DEFAULT_ALLOCATOR<loaded_function_entry>> function_entries;
		std::vector<uint32_t> available_function_ids;
		
		spp::sparsetable<table_entry, SPP_DEFAULT_ALLOCATOR<table_entry>> table_entries;
		std::vector<uint64_t> available_table_ids;
		uint64_t next_table_id;
		std::multimap<uint32_t, gc_block> free_tables;
		spp::sparse_hash_set<char*> active_strs;

		spp::sparsetable<value, SPP_DEFAULT_ALLOCATOR<value>> constants;
		spp::sparse_hash_map<uint64_t, uint32_t> added_constant_hashes;
		std::vector<uint32_t> available_constant_ids;

		spp::sparsetable<foreign_resource, SPP_DEFAULT_ALLOCATOR<foreign_resource>> foreign_resources;

		error type_error(vtype expected, vtype got, uint32_t ip);

		std::optional<gc_block> allocate_block(uint32_t element_count);
		std::optional<uint64_t> allocate_table(uint32_t element_count);
		bool reallocate_table(uint64_t table, uint32_t element_count);
		bool reallocate_table(uint64_t table, uint32_t max_elem_extend, uint32_t min_elem_extend);

		void garbage_collect(gc_collection_mode mode);

		uint32_t emit_function_start(std::vector<instruction>& instructions);
		uint32_t add_constant(value constant);

		uint32_t add_constant_strhash(uint64_t str_hash) {
			return add_constant(value(vtype::INTERNAL_CONSTHASH, hash_combine(str_hash, (uint64_t)vtype::STRING)));
		}
		
		uint32_t add_constant_key(value key) {
			return add_constant(value(vtype::INTERNAL_CONSTHASH, key.compute_hash()));
		}

		std::optional<source_loc> loc_from_ip(uint32_t ip) {
			auto it = ip_src_locs.upper_bound(ip);
			if (it != ip_src_locs.begin()) {
				it--;
				return it->second;
			}
			return std::nullopt;
		}

		error make_error(etype type, std::optional<std::string> msg, uint32_t ip) {
			std::vector<std::pair<std::optional<source_loc>, uint32_t>> stack_trace;
			for (auto it = return_stack.begin(); it != return_stack.end(); ) {
				uint32_t ip = *it;
				uint32_t repeats = 0;
				do {
					it++;
					repeats++;
				} while (it != return_stack.end() && ip == *it);
				stack_trace.push_back(std::make_pair(loc_from_ip(ip), repeats));
			}
			return error(type, msg, loc_from_ip(ip), stack_trace);
		}

		friend class HulaScript::Compilation::compiler;
	};
}