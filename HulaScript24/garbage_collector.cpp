#include <cstdint>
#include <queue>
#include <set>
#include <vector>
#include <cassert>
#include <algorithm>
#include <cstring>
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
		if ((table_offset + element_count) > max_table) {
			return std::nullopt; //out of memory cannot allocate table
		}
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
	auto ptr = (std::pair<uint64_t, uint32_t>*)(element_count > 0 ? malloc(element_count * sizeof(std::pair<uint64_t, uint32_t>)) : NULL);

	if (!res.has_value() || (element_count > 0 && ptr == NULL)) {
		return std::nullopt;
	}

	uint64_t id;
	if (available_table_ids.empty()) {
		id = next_table_id;
		next_table_id++;
		table_entries.resize(next_table_id);
	}
	else {
		id = available_table_ids.back();
		available_table_ids.pop_back();
	}

	table_entry new_entry = {
		.key_hashes = ptr,
		.key_hash_capacity = element_count,
		.used_elems = 0,
		.block = res.value()
	};
	table_entries.set(id, new_entry);
	
	return id;
}

//elements are not initialized by default
bool instance::reallocate_table(uint64_t table_id, uint32_t element_count) {
	table_entry& entry = table_entries.unsafe_get(table_id);

	if (element_count > entry.block.allocated_capacity) { //expand allocation
		std::optional<gc_block> alloc_res = allocate_block(element_count);
		if (!alloc_res.has_value()) {
			return false;
		}
		auto new_key_hashes = (std::pair<uint64_t, uint32_t>*)realloc(entry.key_hashes, element_count * sizeof(std::pair<uint64_t, uint32_t>));
		if (new_key_hashes == NULL) {
			return false;
		}
		entry.key_hashes = new_key_hashes;

		gc_block alloced_entry = alloc_res.value();
		std::memmove(&table_elems[alloced_entry.table_start], &table_elems[entry.block.table_start], entry.used_elems * sizeof(value));

		if (entry.block.allocated_capacity > 0) {
			free_tables.insert({ entry.block.allocated_capacity, entry.block });
		}
		entry.block = alloced_entry;
		return true;
	}
	else
		return false;
}

bool instance::reallocate_table(uint64_t table_id, uint32_t max_elem_extend, uint32_t min_elem_extend) {
	for (uint32_t size = max_elem_extend; size >= min_elem_extend; size--) {
		if (reallocate_table(table_id, table_entries.unsafe_get(table_id).block.allocated_capacity + size))
			return true;
	}
	return false;
}

void instance::garbage_collect(gc_collection_mode mode) {


#define PUSH_TRACE(TO_TRACE) switch(TO_TRACE.type()) { case vtype::TABLE: tables_to_mark.push(TO_TRACE.table_id()); break;\
														case vtype::STRING: marked_strs.insert(TO_TRACE.str()); break;\
														case vtype::CLOSURE: { auto closure_info = TO_TRACE.closure();\
														functions_to_mark.push(closure_info.first); tables_to_mark.push(closure_info.second); break; } }

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
			return_stack.clear();
			extended_offsets.clear();
		}
	}

#undef PUSH_TRACE

	//mark all used tables
	std::set<uint64_t> marked_tables;

	while (!tables_to_mark.empty())
	{
		uint64_t id = tables_to_mark.front();
		tables_to_mark.pop();
		table_entry& entry = table_entries.unsafe_get(id);

		marked_tables.emplace(id);

		for (uint_fast32_t i = 0; i < entry.used_elems; i++) {
			value val = table_elems[i + entry.block.table_start];
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
				if (!marked_tables.contains(closure_info.second)) {
					tables_to_mark.push(closure_info.second);
				}
				break;
			}
			}
		}
	}

	auto cmp_function_by_start = [this](uint32_t a, uint32_t b) -> bool {
		return function_entries.unsafe_get(a).start_address < function_entries.unsafe_get(b).start_address;
	};
	std::set<uint32_t, decltype(cmp_function_by_start)> marked_functions(cmp_function_by_start);
	while (!functions_to_mark.empty()) {
		uint32_t id = functions_to_mark.front();
		functions_to_mark.pop();

		if (marked_functions.contains(id)) {
			continue;
		}
		marked_functions.insert(id);
		for (char* refed_str : function_entries.unsafe_get(id).referenced_const_strs) {
			marked_strs.insert(refed_str);
		}
		for (uint32_t refed_function : function_entries.unsafe_get(id).referenced_func_ids) {
			functions_to_mark.push(refed_function);
		}
	}

	//sweep unreachable tables
	for (auto it = table_entries.ne_cbegin(); it != table_entries.ne_cend();) {
		size_t pos = table_entries.get_pos(it);
		if (!marked_tables.contains(pos)) {
			available_table_ids.push_back(pos);

			free(it->key_hashes);
			it = table_entries.erase(it);
		}
		else {
			it++;
		}
	}

	//free unreachable strings
	for (auto it = active_strs.begin(); it != active_strs.end();)
	{
		if (!marked_strs.contains(*it)) {
			uint64_t hash = hash_combine(str_hash(*it), vtype::STRING);
			auto it2 = added_constant_hashes.find(hash);
			if (it2 != added_constant_hashes.end()) {
				available_constant_ids.push_back(it2->second);
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
		return table_entries.unsafe_get(a).block.table_start < table_entries.unsafe_get(b).block.table_start;
	});
	size_t new_table_offset = 0;
	for (uint64_t id : sorted_marked) {
		instance::table_entry& entry = table_entries.unsafe_get(id);

		if (entry.block.table_start == new_table_offset) {
			new_table_offset += entry.block.allocated_capacity;
			continue;
		}

		std::memmove(&table_elems[new_table_offset], &table_elems[entry.block.table_start], entry.used_elems * sizeof(value));

		entry.block.table_start = new_table_offset;
		new_table_offset += entry.block.allocated_capacity;
	}
	table_offset = new_table_offset;
	free_tables.clear();
	available_table_ids.shrink_to_fit();
	available_constant_ids.shrink_to_fit();
	available_function_ids.shrink_to_fit();

	evaluation_stack.shrink_to_fit();
	scratchpad_stack.shrink_to_fit();
	return_stack.shrink_to_fit();
	extended_offsets.shrink_to_fit();

	if (mode >= gc_collection_mode::FINALIZE_COLLECT_ERROR) {
		//remove unreachable functions
		for (auto it = function_entries.ne_cbegin(); it != function_entries.ne_cend(); it++) {
			size_t pos = function_entries.get_pos(it);
			if (!marked_functions.contains(pos)) {
				available_function_ids.push_back(pos);
				it = function_entries.erase(it);
			}
			else {
				it++;
			}
		}

		//compact instructions of used functions only
		uint32_t current_ip = 0;
		std::vector<std::pair<uint32_t, source_loc>> to_reinsert;
		for (uint32_t id : marked_functions) {
			instance::loaded_function_entry& entry = function_entries.unsafe_get(id);

			if (entry.start_address == current_ip) {
				current_ip += entry.length;
				continue;
			}

			uint32_t offset = entry.start_address - current_ip;
			for (auto it = ip_src_locs.lower_bound(entry.start_address); it != ip_src_locs.lower_bound(entry.start_address + entry.length);) {
				to_reinsert.push_back(std::make_pair(it->first - offset, it->second));
				it = ip_src_locs.erase(it);
			}

			auto start_it = loaded_instructions.begin() + entry.start_address;
			std::move(start_it, start_it + entry.length, loaded_instructions.begin() + current_ip);

			entry.start_address = current_ip;
			current_ip += entry.length;
		}
		for (auto it = ip_src_locs.lower_bound(current_ip); it != ip_src_locs.end();) {
			it = ip_src_locs.erase(it);
		}
		for (auto new_ip_src_loc : to_reinsert) {
			ip_src_locs.insert(new_ip_src_loc);
		}

		loaded_instructions.erase(loaded_instructions.begin() + current_ip, loaded_instructions.end());
		loaded_instructions.shrink_to_fit();
	}
}