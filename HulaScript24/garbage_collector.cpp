#include <cstdint>
#include <queue>
#include <set>
#include <vector>
#include <cassert>
#include "hash.h"
#include "instance.h"

using namespace HulaScript;

std::optional<instance::table_entry> instance::allocate_table_no_id(uint32_t element_count) {
	auto free_table_it = free_tables.lower_bound({ .allocated_capacity = element_count });
	if (free_table_it != free_tables.end()) {
		table_entry new_entry = {
			.table_start = free_table_it->table_start,
			.allocated_capacity = element_count
		};
		uint32_t unused_elem_count = free_table_it->allocated_capacity - element_count;
		free_tables.erase(free_table_it);

		if (unused_elem_count > 0) {
			free_tables.insert({
				.table_start = new_entry.table_start + new_entry.allocated_capacity,
				.allocated_capacity = unused_elem_count
			});
		}

		return std::make_optional(new_entry);
	}

	if (table_offset + element_count > max_table) {
		garbage_collect();
		if (table_offset + element_count)
			return std::nullopt; //out of memory cannot allocate table
	}

	table_entry new_entry = {
		.table_start = table_offset, //table_start,
		.allocated_capacity = element_count //length
	};
	table_offset += element_count;

	return std::make_optional(new_entry);
}

//elements are initialized by default to nil
std::optional<uint64_t> instance::allocate_table(uint32_t element_count) {
	std::optional<table_entry> res = allocate_table_no_id(element_count);
	if (!res.has_value())
		return std::nullopt;

	uint64_t id;
	if (available_table_ids.empty())
		id = max_table_id++;
	else 
	{
		id = available_table_ids.front();
		available_table_ids.pop();
	}
	
	table_entry table_entry = res.value();
	table_entry.used_elems = 0;
	table_entries.insert({ id, table_entry });
	return id;
}

//elements are not initialized by default
bool instance::reallocate_table(uint64_t table_id, uint32_t element_count) {
	auto it = table_entries.find(table_id);
	assert(it != table_entries.end());

	table_entry& entry = it->second;

	if (element_count > entry.allocated_capacity) { //expand allocation
		std::optional<table_entry> alloc_res = allocate_table_no_id(element_count);
		if (!alloc_res.has_value())
			return false;

		size_t old_start = entry.table_start;
		uint32_t old_len = entry.used_elems;
		entry = alloc_res.value();
		std::memmove(&table_elems[entry.table_start], &table_elems[old_start], old_len);
		entry.used_elems = old_len;

		free_tables.insert({
			.table_start = entry.table_start,
			.allocated_capacity = entry.allocated_capacity
		});
		return true;
	}
	else if(element_count < entry.allocated_capacity) {
		entry = {
			.table_start = entry.table_start,
			.allocated_capacity = element_count
		};

		free_tables.insert({
			.table_start = entry.table_start + element_count,
			.allocated_capacity = entry.allocated_capacity - element_count
		});
		return true;
	}
	else
		return true;
}

bool instance::reallocate_table(uint64_t table_id, uint32_t max_elem_extend, uint32_t min_elem_extend) {
	auto it = table_entries.find(table_id);
	assert(it != table_entries.end());

	for (uint32_t size = max_elem_extend; size >= min_elem_extend; size--) {
		if (reallocate_table(table_id, it->second.allocated_capacity + size))
			return true;
	}
	return false;
}

void instance::garbage_collect() {
#define PUSH_TRACE(TO_TRACE) { if(TO_TRACE.is_gc_type()) { tables_to_mark.push(TO_TRACE.data.table_id); }\
								else if(TO_TRACE.type == value::vtype::STRING) { marked_strs.insert(TO_TRACE.data.str); } \
								if(TO_TRACE.type == value::vtype::CLOSURE) { marked_functions.insert(TO_TRACE.func_id); } }

	std::queue<uint64_t> tables_to_mark;
	std::set<char*> marked_strs;
	std::set<uint32_t> marked_functions;

	for (int i = 0; i < local_offset + extended_local_offset; i++)
		PUSH_TRACE(local_elems[i]);
	for (int i = 0; i < global_offset; i++)
		PUSH_TRACE(global_elems[i]);

	for (instance::value eval_value : evaluation_stack)
		PUSH_TRACE(eval_value);
	for (instance::value scratch_value : scratchpad_stack)
		PUSH_TRACE(scratch_value);

#undef PUSH_TRACE

	//sort table ids by table start address
	auto cmp_by_table_start = [this](uint64_t a, uint64_t b) -> bool {
		return table_entries[a].table_start < table_entries[b].table_start;
	};
	
	//mark all used tables
	std::set<uint64_t, decltype(cmp_by_table_start)> marked_tables(cmp_by_table_start);

	while (!tables_to_mark.empty())
	{
		uint64_t id = tables_to_mark.front();
		tables_to_mark.pop();
		instance::table_entry entry = table_entries[id];

		marked_tables.emplace(id);
		
		for (int i = 0; i < entry.used_elems; i++) {
			instance::value val = table_elems[i + entry.table_start];
			if (val.is_gc_type() && !marked_tables.contains(val.data.table_id)) {
				tables_to_mark.push(val.data.table_id);
			}
			else if (val.type == value::vtype::STRING) {
				marked_strs.insert(val.data.str);
			}
			if (val.type == value::vtype::CLOSURE) {
				std::queue<uint32_t> functions_to_mark;
				functions_to_mark.push(val.func_id);
				while (!functions_to_mark.empty()) {
					uint32_t id = functions_to_mark.front();
					functions_to_mark.pop();

					if (marked_functions.contains(id)) {
						continue;
					}
					marked_functions.insert(id);
					for (char* refed_str : function_entries[id].referenced_const_strs) {
						marked_strs.insert(refed_str);
					}
					for (uint32_t refed_function : function_entries[id].referenced_func_ids) {
						functions_to_mark.push(refed_function);
					}
				}
			}
		}
	}

	//sweep unreachable tables
	for (auto table_it = table_entries.begin(); table_it != table_entries.end(); table_it++) {
		if (!marked_tables.contains(table_it->first)) {
			available_table_ids.push(table_it->first);
			table_it = table_entries.erase(table_it);
		}
	}
	//free unreachable strings
	for (auto it = active_strs.begin(); it != active_strs.end(); it++)
	{
		if (!marked_strs.contains(*it)) {
			uint64_t hash = str_hash(*it);
			auto it2 = added_constant_hashes.find(hash);
			if (it2 != added_constant_hashes.end()) {
				constants.erase(constants.begin() + it2->second);
				added_constant_hashes.erase(hash);
			}
			
			free(*it);
			it = active_strs.erase(it);
		}
	}

	//compact used tables
	size_t new_table_offset = 0;
	for (uint64_t id : marked_tables) {
		instance::table_entry entry = table_entries[id];

		if (entry.table_start == new_table_offset)
			continue;

		std::memmove(&table_elems[entry.table_start], &table_elems[new_table_offset], entry.used_elems);

		table_entries[id].table_start = new_table_offset;
		new_table_offset += entry.used_elems;
	}

	table_offset = new_table_offset;
	free_tables.clear();
}

void instance::finalize_collect(const std::vector<instruction>& instructions) {
	std::queue<uint64_t> tables_to_mark;
	std::set<char*> marked_strs;

	auto cmp_function_by_start = [this](uint32_t a, uint32_t b) -> bool {
		return function_entries[a].start_address < function_entries[b].start_address;
	};
	std::set<uint32_t, decltype(cmp_function_by_start)> marked_functions(cmp_function_by_start);

#define PUSH_TRACE(TO_TRACE) { if(TO_TRACE.is_gc_type()) { tables_to_mark.push(TO_TRACE.data.table_id); }\
								else if(TO_TRACE.type == value::vtype::STRING) { marked_strs.insert(TO_TRACE.data.str); }\
								if(TO_TRACE.type == value::vtype::CLOSURE) { marked_functions.insert(TO_TRACE.func_id); } }

	for (int i = 0; i < local_offset + extended_local_offset; i++)
		PUSH_TRACE(local_elems[i]);
	for (int i = 0; i < global_offset; i++)
		PUSH_TRACE(global_elems[i]);

#undef PUSH_TRACE

	//clear temporary stacks
	loaded_functions.clear();
	evaluation_stack.clear();
	scratchpad_stack.clear();

	//mark all used tables
	std::set<uint64_t> marked_tables;
	while (!tables_to_mark.empty())
	{
		uint64_t id = tables_to_mark.front();
		tables_to_mark.pop();
		instance::table_entry entry = table_entries[id];

		marked_tables.emplace(id);

		for (int i = 0; i < entry.used_elems; i++) {
			instance::value val = table_elems[i + entry.table_start];
			if (val.is_gc_type() && !marked_tables.contains(val.data.table_id)) {
				tables_to_mark.push(val.data.table_id);
			}
			else if (val.type == value::vtype::STRING) {
				marked_strs.insert(val.data.str);
			}
			if (val.type == value::vtype::CLOSURE) {
				std::queue<uint32_t> functions_to_mark;
				functions_to_mark.push(val.func_id);
				while (!functions_to_mark.empty()) {
					uint32_t id = functions_to_mark.front();
					functions_to_mark.pop();

					if (marked_functions.contains(id)) {
						continue;
					}
					marked_functions.insert(id);
					for (char* refed_str : function_entries[id].referenced_const_strs) {
						marked_strs.insert(refed_str);
					}
					for (uint32_t refed_function : function_entries[id].referenced_func_ids) {
						functions_to_mark.push(refed_function);
					}
				}
			}
		}
	}

	//free unreachable strings
	for (auto it = active_strs.begin(); it != active_strs.end(); it++)
	{
		if (!marked_strs.contains(*it)) {
			uint64_t hash = str_hash(*it);
			auto it2 = added_constant_hashes.find(hash);
			if (it2 != added_constant_hashes.end()) {
				constants.erase(constants.begin() + it2->second);
				added_constant_hashes.erase(hash);
			}

			free(*it);
			it = active_strs.erase(it);
		}
	}

	//remove unreachable functions
	for (auto it = function_entries.begin(); it != function_entries.end(); it++) {
		if (!marked_functions.contains(it->first)) {
			available_function_ids.push(it->first);
			it = function_entries.erase(it);
		}
	}

	//compact instructions of used functions only
	uint32_t current_ip = 0;
	for (uint32_t id : marked_functions) {
		auto it = function_entries.find(id);

		auto ins_begin = instructions.begin() + it->second.start_address;
		loaded_functions.insert(loaded_functions.begin() + current_ip, ins_begin, ins_begin + it->second.length);

		it->second.start_address = current_ip;
		current_ip += it->second.length;
	}
}