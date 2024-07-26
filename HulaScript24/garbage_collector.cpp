#include <cstdint>
#include <queue>
#include <set>
#include <vector>
#include <cassert>
#include <algorithm>
#include "hash.h"
#include "instance.h"

using namespace HulaScript::Runtime;

std::optional<instance::gc_block> instance::allocate_block(uint32_t element_count) {
	auto free_table_it = free_tables.lower_bound(element_count);
	if (free_table_it != free_tables.end()) {
		gc_block new_entry = {
			.table_start = free_table_it->second.table_start,
			.allocated_capacity = element_count
		};
		uint32_t unused_elem_count = free_table_it->second.allocated_capacity - element_count;
		free_tables.erase(free_table_it);

		if (unused_elem_count > 0) {
			free_tables.insert({ unused_elem_count, {
				.table_start = new_entry.table_start + new_entry.allocated_capacity,
				.allocated_capacity = unused_elem_count
			}});
		}

		return std::make_optional(new_entry);
	}

	if (table_offset + element_count > max_table) {
		garbage_collect(gc_collection_mode::STANDARD);
		if (table_offset + element_count)
			return std::nullopt; //out of memory cannot allocate table
	}

	gc_block new_entry = {
		.table_start = table_offset, //table_start,
		.allocated_capacity = element_count //length
	};
	table_offset += element_count;

	return std::make_optional(new_entry);
}

//elements are initialized by default to nil
std::optional<uint64_t> instance::allocate_table(uint32_t element_count) {
	std::optional<gc_block> res = allocate_block(element_count);
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
	
	table_entry table_entry = {
		.used_elems = 0,
		.table_start = res.value().table_start,
		.allocated_capacity = res.value().allocated_capacity
	};
	table_entries.insert({ id, table_entry });
	return id;
}

//elements are not initialized by default
bool instance::reallocate_table(uint64_t table_id, uint32_t element_count) {
	auto it = table_entries.find(table_id);
	assert(it != table_entries.end());

	table_entry& entry = it->second;

	if (element_count > entry.allocated_capacity) { //expand allocation
		std::optional<gc_block> alloc_res = allocate_block(element_count);
		if (!alloc_res.has_value())
			return false;

		gc_block alloced_entry = alloc_res.value();
		std::memmove(&table_elems[alloced_entry.table_start], &table_elems[entry.table_start], entry.used_elems * sizeof(value));

		free_tables.insert({ entry.allocated_capacity, {
			.table_start = entry.table_start,
			.allocated_capacity = entry.allocated_capacity
		} });

		entry.table_start = alloced_entry.table_start;
		entry.allocated_capacity = alloced_entry.allocated_capacity;

		return true;
	}
	else if(element_count < entry.allocated_capacity) {
		uint32_t free_capacity = entry.allocated_capacity - element_count;
		entry.allocated_capacity = element_count;
		free_tables.insert({ free_capacity, {
			.table_start = entry.table_start + element_count,
			.allocated_capacity = free_capacity
		} });
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

void instance::garbage_collect(gc_collection_mode mode) {


#define PUSH_TRACE(TO_TRACE) switch(TO_TRACE.type()) { case vtype::TABLE: tables_to_mark.push(TO_TRACE.table_id()); break;\
														case vtype::STRING: marked_strs.insert(TO_TRACE.str()); break;\
														case vtype::CLOSURE: { auto closure_info = TO_TRACE.closure();\
														functions_to_mark.push(closure_info.first); tables_to_mark.push(closure_info.second); break; } }


	/*{ if(TO_TRACE.is_gc_type()) { tables_to_mark.push(TO_TRACE.table_id()); }\
								else if(TO_TRACE.type() == vtype::STRING) { marked_strs.insert(TO_TRACE.str()); } \
								if(TO_TRACE.type() == vtype::CLOSURE) { marked_functions.insert(TO_TRACE.closure().first); } }*/

	std::queue<uint64_t> tables_to_mark;
	std::set<char*> marked_strs;
	std::queue<uint32_t> functions_to_mark;

	for (uint_fast32_t i = 0; i < local_offset + extended_local_offset; i++)
		PUSH_TRACE(local_elems[i]);
	for (uint_fast32_t i = 0; i < global_offset; i++)
		PUSH_TRACE(global_elems[i]);

	if (mode == gc_collection_mode::STANDARD) {
		for (value eval_value : evaluation_stack)
			PUSH_TRACE(eval_value);
		for (value scratch_value : scratchpad_stack)
			PUSH_TRACE(scratch_value);
	}
	else {
		scratchpad_stack.clear();
		if (mode == gc_collection_mode::FINALIZE_COLLECT_RETURN) {
			PUSH_TRACE(evaluation_stack.back());
		}
		else {
			evaluation_stack.clear();
		}
	}

#undef PUSH_TRACE

	//mark all used tables
	std::set<uint64_t> marked_tables;

	while (!tables_to_mark.empty())
	{
		uint64_t id = tables_to_mark.front();
		tables_to_mark.pop();
		instance::table_entry entry = table_entries[id];

		marked_tables.emplace(id);

		for (uint_fast32_t i = 0; i < entry.used_elems; i++) {
			value val = table_elems[i + entry.table_start];
			switch (val.type())
			{
			case vtype::TABLE:
				if (!marked_tables.contains(val.table_id())) {
					tables_to_mark.push(val.table_id());
				}
				break;
			case vtype::STRING:
				marked_strs.insert(val.str());
				break;
			case vtype::CLOSURE:
			{
				auto closure_info = val.closure();
				functions_to_mark.push(closure_info.first);
				tables_to_mark.push(closure_info.second);
				break;
			}
			}
		}
	}

	auto cmp_function_by_start = [this](uint32_t a, uint32_t b) -> bool {
		return function_entries[a].start_address < function_entries[b].start_address;
		};
	std::set<uint32_t, decltype(cmp_function_by_start)> marked_functions(cmp_function_by_start);
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

	//sweep unreachable tables
	for (auto table_it = table_entries.begin(); table_it != table_entries.end();) {
		if (!marked_tables.contains(table_it->first)) {
			available_table_ids.push(table_it->first);
			table_it = table_entries.erase(table_it);
		}
		else {
			table_it++;
		}
	}
	//free unreachable strings
	for (auto it = active_strs.begin(); it != active_strs.end();)
	{
		if (!marked_strs.contains(*it)) {
			uint64_t hash = hash_combine(str_hash(*it), vtype::STRING);
			auto it2 = added_constant_hashes.find(hash);
			if (it2 != added_constant_hashes.end()) {
				available_constant_ids.push(it2->second);
				added_constant_hashes.erase(hash);
			}

			free(*it);
			it = active_strs.erase(it);
		}
		else {
			it++;
		}
	}

	//compact used tables

	//sort table ids by table start address
	std::vector<uint64_t> sorted_marked(marked_tables.begin(), marked_tables.end());
	std::ranges::sort(sorted_marked, [this](uint64_t a, uint64_t b) -> bool {
		return table_entries[a].table_start < table_entries[b].table_start;
	});
	size_t new_table_offset = 0;
	for (uint64_t id : sorted_marked) {
		instance::table_entry& entry = table_entries[id];

		if (entry.table_start == new_table_offset) {
			new_table_offset += entry.used_elems;
			continue;
		}

		std::memmove(&table_elems[new_table_offset], &table_elems[entry.table_start], entry.used_elems * sizeof(value));

		entry.table_start = new_table_offset;
		new_table_offset += entry.used_elems;
	}
	table_offset = new_table_offset;
	free_tables.clear();

	if (mode >= gc_collection_mode::FINALIZE_COLLECT_ERROR) {
		//remove unreachable functions
		for (auto it = function_entries.begin(); it != function_entries.end();) {
			if (!marked_functions.contains(it->first)) {
				available_function_ids.push(it->first);
				it = function_entries.erase(it);
			}
			else
				it++;
		}

		//compact instructions of used functions only
		uint32_t current_ip = 0;
		for (uint32_t id : marked_functions) {
			instance::loaded_function_entry& entry = function_entries[id];

			if (entry.start_address == current_ip) {
				current_ip += entry.length;
				continue;
			}

			auto start_it = loaded_instructions.begin() + entry.start_address;
			std::move(start_it, start_it + entry.length, loaded_instructions.begin() + current_ip);

			entry.start_address = current_ip;
			current_ip += entry.length;
		}
		loaded_instructions.erase(loaded_instructions.begin() + current_ip, loaded_instructions.end());
	}
}