#include <cassert>
#include <sstream>
#include <algorithm>
#include "instance.h"

using namespace HulaScript::Runtime;

std::variant<value, error> instance::execute() {
	auto table_hashid_comparator = [](std::pair<uint64_t, uint32_t> a, std::pair<uint64_t, uint32_t> b) -> bool { return a.first < b.first; };

	instruction* instructions = loaded_instructions.data();
	uint_fast32_t ip = start_ip;
	local_offset = 0;

#define LOAD_OPERAND(OPERAND_NAME, EXPECTED_TYPE)	value OPERAND_NAME = evaluation_stack.back();\
													evaluation_stack.pop_back();\
													if(OPERAND_NAME.type() != EXPECTED_TYPE) {\
														current_error = type_error(EXPECTED_TYPE, OPERAND_NAME.type(), ip);\
														goto stop_exec;\
													}\

	std::optional<error> current_error = std::nullopt;
	while (ip != loaded_instructions.size())
	{
		instruction& ins = instructions[ip];

		switch (ins.op)
		{
		//arithmetic operations
		case opcode::ADD: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() + b.number()));
			goto next_ins;
		}
		case opcode::SUB: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() - b.number()));
			goto next_ins;
		}
		case opcode::MUL: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() * b.number()));
			goto next_ins;
		}
		case opcode::DIV: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() / b.number()));
			goto next_ins;
		}
		case opcode::MOD: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(fmod(a.number(), b.number())));
			goto next_ins;
		}
		case opcode::EXP: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(pow(a.number(), b.number())));
			goto next_ins;
		}
		case opcode::LESS: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() < b.number()));
			goto next_ins;
		}
		case opcode::MORE: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() > b.number()));
			goto next_ins;
		}
		case opcode::LESS_EQUAL: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() <= b.number()));
			goto next_ins;
		}
		case opcode::MORE_EQUAL: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() >= b.number()));
			goto next_ins;
		}
		case opcode::EQUALS: {
			value b = evaluation_stack.back();
			evaluation_stack.pop_back();
			value a = evaluation_stack.back();
			evaluation_stack.pop_back();
			evaluation_stack.push_back(value(a.compute_hash() == b.compute_hash()));
			goto next_ins;
		}
		case opcode::NOT_EQUALS: {
			value b = evaluation_stack.back();
			evaluation_stack.pop_back();
			value a = evaluation_stack.back();
			evaluation_stack.pop_back();
			evaluation_stack.push_back(value(a.compute_hash() != b.compute_hash()));
			goto next_ins;
		}
		case opcode::AND: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() != 0 && b.number() != 0));
			goto next_ins;
		}
		case opcode::OR: {
			LOAD_OPERAND(b, vtype::NUMBER);
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() != 0 || b.number() != 0));
			goto next_ins;
		}
		case opcode::NEGATE: {
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(-a.number()));
			goto next_ins;
		}
		case opcode::NOT: {
			LOAD_OPERAND(a, vtype::NUMBER);
			evaluation_stack.push_back(value(a.number() == 0));
			goto next_ins;
		}

		//variable operations
		case opcode::LOAD_LOCAL:
			evaluation_stack.push_back(local_elems[ins.operand + local_offset]);
			goto next_ins;
		case opcode::LOAD_GLOBAL:
			evaluation_stack.push_back(global_elems[ins.operand]);
			goto next_ins;
		case opcode::STORE_LOCAL:
			local_elems[local_offset + ins.operand] = evaluation_stack.back();
			goto next_ins;
		case opcode::STORE_GLOBAL:
			global_elems[ins.operand] = evaluation_stack.back();
			evaluation_stack.pop_back();
			goto next_ins;
		case opcode::DECL_TOPLVL_LOCAL:
			assert(local_offset == 0);
			top_level_local_offset++;
			[[fallthrough]];
		case opcode::DECL_LOCAL:
			assert(extended_local_offset == ins.operand);
			local_elems[local_offset + extended_local_offset] = evaluation_stack.back();
			evaluation_stack.pop_back();
			extended_local_offset++;
			goto next_ins;
		case opcode::DECL_GLOBAL:
			assert(global_offset == ins.operand);
			global_elems[global_offset] = evaluation_stack.back();
			evaluation_stack.pop_back();
			global_offset++;
			goto next_ins;
		case opcode::UNWIND_LOCALS:
			extended_local_offset -= ins.operand;
			goto next_ins;
		case opcode::PROBE_LOCALS:
			if (local_offset + extended_local_offset + ins.operand > max_locals) {
				current_error = make_error(etype::MEMORY, "Stack Overflow: ran out of memory while allocating local.", ip);
				goto stop_exec;
			}
			goto next_ins;
		case opcode::PROBE_GLOBALS:
			if (global_offset + ins.operand > max_locals) {
				current_error = make_error(etype::MEMORY, "Stack Overflow: ran out of memory while allocating globals.", ip);
				goto stop_exec;
			}
			goto next_ins;

		//other miscellaneous operations
		case opcode::LOAD_CONSTANT:
			evaluation_stack.push_back(constants.unsafe_get(ins.operand));
			goto next_ins;
		case opcode::PUSH_NIL:
			evaluation_stack.push_back(value());
			goto next_ins;
		case opcode::DISCARD_TOP:
			evaluation_stack.pop_back();
			goto next_ins;
		case opcode::POP_SCRATCHPAD:
			evaluation_stack.push_back(scratchpad_stack.back());
			scratchpad_stack.pop_back();
			goto next_ins;
		case opcode::PEEK_SCRATCHPAD:
			evaluation_stack.push_back(scratchpad_stack.back());
			goto next_ins;
		case opcode::PUSH_SCRATCHPAD:
			scratchpad_stack.push_back(evaluation_stack.back());
			evaluation_stack.pop_back();
			goto next_ins;
		case opcode::DUPLICATE:
			evaluation_stack.push_back(evaluation_stack.back());
			goto next_ins;

		//table operations
		case opcode::LOAD_TABLE_ELEM:
		{
			value key_val = evaluation_stack.back();
			evaluation_stack.pop_back();

			auto table_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			if (table_val.type() == vtype::FOREIGN_RESOURCE) {
				std::unique_ptr<foreign_resource>& resource = foreign_resources.unsafe_get(table_val.capture_id());
				evaluation_stack.push_back(resource->load_key(key_val));
				goto next_ins;
			}
			else if (table_val.type() != vtype::TABLE) {
				current_error = type_error(vtype::TABLE, table_val.type(), ip);
				goto stop_exec;
			}

			//LOAD_OPERAND(table_val, vtype::TABLE);

			table_entry& table_entry = table_entries.unsafe_get(table_val.table_id());
			uint64_t hash = key_val.compute_key_hash();

			uint32_t low = 0;
			uint32_t high = table_entry.used_elems;
			while (low < high)
			{
				uint32_t mid = (high & low) + ((high ^ low) >> 1);
				std::pair<uint64_t, uint32_t>& current = table_entry.key_hashes[mid];

				if (current.first == hash) {
					evaluation_stack.push_back(table_elems[table_entry.block.table_start + current.second]);
					goto next_ins;
				}
				else if(hash < current.first) {
					high = mid;
				}
				else {
					low = mid + 1;
				}
			}
			evaluation_stack.push_back(value());
			goto next_ins;
		}
		case opcode::STORE_TABLE_ELEM: {
			value store_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			value key_val = evaluation_stack.back();
			evaluation_stack.pop_back();

			auto table_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			if (table_val.type() == vtype::FOREIGN_RESOURCE) {
				std::unique_ptr<foreign_resource>& resource = foreign_resources.unsafe_get(table_val.capture_id());
				evaluation_stack.push_back(resource->set_key(key_val, store_val));
				goto next_ins;
			}
			else if (table_val.type() != vtype::TABLE) {
				current_error = type_error(vtype::TABLE, table_val.type(), ip);
				goto stop_exec;
			}

			table_entry& table_entry = table_entries.unsafe_get(table_val.table_id());
			uint64_t hash = key_val.compute_key_hash();

			evaluation_stack.push_back(store_val);

			uint32_t low = 0;
			uint32_t high = table_entry.used_elems;
			while (low < high)
			{
				//mid = (high + low) / 2;
				uint32_t mid = (high & low) + ((high ^ low) >> 1);
				std::pair<uint64_t, uint32_t>& current = table_entry.key_hashes[mid];

				if (current.first == hash) {
					table_elems[table_entry.block.table_start + current.second] = store_val;
					goto next_ins;
				}
				else if (hash < current.first) {
					high = mid;
				}
				else {
					low = mid + 1;
				}
			}

			//protect operands from potential garbage collect during allocate
			if (table_entry.used_elems == table_entry.key_hash_capacity) {
				table_entry.key_hash_capacity += 1;
				auto new_buffer = (std::pair<uint64_t, uint32_t>*)realloc(table_entry.key_hashes, table_entry.key_hash_capacity * sizeof(std::pair<uint64_t, uint32_t>));
				if (new_buffer == NULL) {
					current_error = make_error(etype::MEMORY, "Cannot add new element to table.", ip);
					goto stop_exec;
				}
				table_entry.key_hashes = new_buffer;
			}
			
			if (low < table_entry.used_elems) {
				std::memmove(&table_entry.key_hashes[low + 1], &table_entry.key_hashes[low], (table_entry.used_elems - low) * sizeof(std::pair<uint64_t, uint32_t>));
			}
			table_entry.key_hashes[low] = std::make_pair(hash, table_entry.used_elems);
			
			if (table_entry.used_elems == table_entry.block.allocated_capacity) {
				scratchpad_stack.push_back(table_val);
				if (!reallocate_table(table_val.table_id(), 4, 1))
				{
					current_error = make_error(etype::MEMORY, "Failed to add to table.", ip);
					goto stop_exec;
				}
				scratchpad_stack.pop_back();
			}
			table_elems[table_entry.block.table_start + table_entry.used_elems] = store_val;
			table_entry.used_elems++;
			
			goto next_ins;
		}
		case opcode::ALLOCATE_DYN:
		{
			LOAD_OPERAND(length_val, vtype::NUMBER);
			
			uint32_t size = static_cast<uint32_t>(floor(length_val.number()));
			std::optional<uint64_t> res = allocate_table(size);
			if (!res.has_value()) {
				std::stringstream ss;
				ss << "Failed to allocate new table with " << length_val.number() << " elements";
				if (size != length_val.number()) {
					ss << "(rounded to " << size << ")";
				}
				ss << '.';
				current_error = make_error(etype::MEMORY, ss.str(), ip);
				goto stop_exec;
			}
			evaluation_stack.push_back(value(res.value()));
			goto next_ins;
		}
		case opcode::ALLOCATE_FIXED: {
			std::optional<uint64_t> res = allocate_table(ins.operand);
			if (!res.has_value()) {
				std::stringstream ss;
				ss << "Failed to allocate new array with " << ins.operand << " elements.";
				current_error = make_error(etype::MEMORY, ss.str(), ip);
				goto stop_exec;
			}
			evaluation_stack.push_back(value(res.value()));
			goto next_ins;
		}

		//control flow
		case opcode::COND_JUMP_AHEAD:
		{
			LOAD_OPERAND(cond_val, vtype::NUMBER);
			if (cond_val.number() != 0)
				goto next_ins;
		}
		[[fallthrough]];
		case opcode::JUMP_AHEAD:
			ip += ins.operand;
			continue;
		case opcode::COND_JUMP_BACK: //used primarily for do..while
		{
			LOAD_OPERAND(cond_val, vtype::NUMBER);
			if (cond_val.number() == 0)
				goto next_ins;
		}
		[[fallthrough]];
		case opcode::JUMP_BACK:
			ip -= ins.operand;
			continue;
		case opcode::IF_NIL_JUMP_AHEAD:
		{
			if (evaluation_stack.back().type() == vtype::NIL) {
				evaluation_stack.pop_back();
				ip += ins.operand;
				continue;
			}
			goto next_ins;
		}
		case opcode::IFNT_NIL_JUMP_AHEAD: {
			if (evaluation_stack.back().type() == vtype::NIL) {
				evaluation_stack.pop_back();
				goto next_ins;
			}
			else {
				ip += ins.operand;
				continue;
			}
		}

		//function operations
		case opcode::FUNCTION:
		{
			uint32_t id = ins.operand;
			uint32_t end_addr = ip;

			loaded_function_entry entry = {
				.start_address = ip + 1
			};
			spp::sparse_hash_set<uint32_t> referenced_func_ids;
			spp::sparse_hash_set<char*> referenced_strs;
			do {
				switch (instructions[end_addr].op)
				{
				case opcode::CALL_NO_CAPUTRE_TABLE:
					[[fallthrough]];
				case opcode::MAKE_CLOSURE:
					referenced_func_ids.insert(instructions[end_addr].operand);
					break;
				case opcode::LOAD_CONSTANT: {
					value& constant = constants.unsafe_get(instructions[end_addr].operand);
					if (constant.type() == vtype::STRING) {
						referenced_strs.insert(constant.str());
					}
					break;
				}
				}

				end_addr++;
				if (end_addr == loaded_instructions.size()) {
					std::stringstream ss;
					ss << "No matching function end instruction for function instruction at " << ip << '.';
					current_error = make_error(etype::INTERNAL_ERROR, ss.str(), end_addr);
					goto stop_exec;
				}
			} while (instructions[end_addr].op != opcode::FUNCTION_END);

			entry.referenced_func_ids.reserve(referenced_func_ids.size());
			entry.referenced_func_ids.insert(entry.referenced_func_ids.begin(), referenced_func_ids.begin(), referenced_func_ids.end());
			entry.referenced_const_strs.reserve(referenced_strs.size());
			entry.referenced_const_strs.insert(entry.referenced_const_strs.begin(), referenced_strs.begin(), referenced_strs.end());
			entry.length = end_addr - ip;
			ip = end_addr;

			instruction end_ins = instructions[ip];
			entry.parameter_count = end_ins.operand;

			function_entries.set(id, entry);

			ip++;
			continue;
		}
		case opcode::FUNCTION_END: //automatically return if this instruction is ever reached
			evaluation_stack.push_back(value());
			goto return_function;
		case opcode::MAKE_CLOSURE: 
		{
			LOAD_OPERAND(capture_table, vtype::TABLE);
			evaluation_stack.push_back(value(ins.operand, capture_table.table_id()));
			goto next_ins;
		}
		case opcode::CALL: 
		{
			auto fn_val = evaluation_stack.back();
			evaluation_stack.pop_back();

			switch (fn_val.type())
			{
			case vtype::FOREIGN_FUNCTION: {
				value* args = evaluation_stack.data() + (evaluation_stack.size() - ins.operand);
				foreign_function_t func = static_cast<foreign_function_t>(fn_val.raw_ptr());
				auto res = func(args, ins.operand);
				evaluation_stack.erase(evaluation_stack.end() - ins.operand, evaluation_stack.end());

				if (std::holds_alternative<error>(res)) {
					current_error = std::get<error>(res);
					goto stop_exec;
				}
				else {
					evaluation_stack.push_back(std::get<value>(res));
					goto next_ins;
				}
			}
			case vtype::FOREIGN_MEMBER: {
				value* args = evaluation_stack.data() + (evaluation_stack.size() - ins.operand);
				foreign_resource* resource = foreign_resources.unsafe_get(fn_val.capture_id()).get();
				foreign_member_t func = static_cast<foreign_member_t>(fn_val.raw_ptr());
				auto res = func(args, ins.operand, resource);
				evaluation_stack.erase(evaluation_stack.end() - ins.operand, evaluation_stack.end());
				
				if (std::holds_alternative<error>(res)) {
					current_error = std::get<error>(res);
					goto stop_exec;
				}
				else {
					evaluation_stack.push_back(std::get<value>(res));
					goto next_ins;
				}
			}
			case vtype::CLOSURE:
				break;
			default:
				current_error = type_error(vtype::CLOSURE, fn_val.type(), ip);
				goto stop_exec;
			}

			auto fn_closure = fn_val.closure();

			evaluation_stack.push_back(value(fn_closure.second));

			return_stack.push_back(ip);
			loaded_function_entry& fn_entry = function_entries.unsafe_get(fn_closure.first);

			if (fn_entry.parameter_count != ins.operand) { //argument count mismatch
				std::stringstream ss;
				ss << "Function";
				auto func_loc = loc_from_ip(fn_entry.start_address);
				if (func_loc.has_value()) {
					ss << ", at " << func_loc.value().to_print_string() << ',';
				}

				ss << " expected " << fn_entry.parameter_count << " argument(s), but got " << ins.operand << " instead.";
				current_error = make_error(etype::ARGUMENT_COUNT_MISMATCH, ss.str(), ip);
				goto stop_exec;
			}

			local_offset += extended_local_offset;
			extended_offsets.push_back(extended_local_offset);
			extended_local_offset = 0;
			ip = fn_entry.start_address;
			continue;
		}
		case opcode::CALL_NO_CAPUTRE_TABLE: {
			return_stack.push_back(ip);
			loaded_function_entry& fn_entry = function_entries.unsafe_get(ins.operand); 
			
			//no parameter count check - use instruction at your own risk! 

			local_offset += extended_local_offset;
			extended_offsets.push_back(extended_local_offset);
			extended_local_offset = 0;
			ip = fn_entry.start_address;
			continue;
		}
		case opcode::RETURN:
		return_function:
			if (return_stack.empty())
				goto stop_exec;

			ip = return_stack.back();
			return_stack.pop_back();
			extended_local_offset = extended_offsets.back();
			extended_offsets.pop_back();
			local_offset -= extended_local_offset;
			goto next_ins;
		case opcode::INVALID:
			current_error = make_error(etype::INTERNAL_ERROR, "Encountered unexpected invalid instruction", ip);
			goto stop_exec;
		default: {
			std::stringstream ss;
			ss << "Unrecognized opcode " << ins.op << " is unhandled.";
			current_error = make_error(etype::INTERNAL_ERROR, ss.str(), ip);
			goto stop_exec;
		}
		}

	next_ins:
		ip++;
	}
	
	if (evaluation_stack.empty())
		evaluation_stack.push_back(value());
	
stop_exec:
	if (current_error.has_value()) {
		garbage_collect(gc_collection_mode::FINALIZE_COLLECT_ERROR);
		start_ip = static_cast<uint32_t>(loaded_instructions.size());
		extended_local_offset = top_level_local_offset;
		return current_error.value();
	}
	else {
		garbage_collect(gc_collection_mode::FINALIZE_COLLECT_RETURN);
		start_ip = static_cast<uint32_t>(loaded_instructions.size());
		assert(evaluation_stack.size() == 1);
		assert(top_level_local_offset == extended_local_offset);
		value to_return = evaluation_stack.back();
		evaluation_stack.pop_back();
		return to_return;
	}
#undef LOAD_OPERAND
#undef NORMALIZE_ARRAY_INDEX
}

uint32_t instance::emit_function_start(std::vector<instruction>& instructions) {
	uint32_t id;
	if (available_function_ids.empty()) {
		id = static_cast<uint32_t>(function_entries.size());
		function_entries.resize(id + 1);
	}
	else {
		id = available_function_ids.back();
		available_function_ids.pop_back();
	}
	instructions.push_back({ .op = opcode::FUNCTION, .operand = id });
	return id;
}