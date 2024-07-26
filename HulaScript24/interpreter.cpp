#include <cassert>
#include <sstream>
#include <algorithm>
#include "instance.h"

using namespace HulaScript::Runtime;

std::variant<value, error> instance::execute() {
	instruction* instructions = loaded_instructions.data();
	uint_fast32_t ip = start_ip;

#define LOAD_OPERAND(OPERAND_NAME, EXPECTED_TYPE)	value OPERAND_NAME = evaluation_stack.back();\
													evaluation_stack.pop_back();\
													if(OPERAND_NAME.type() != EXPECTED_TYPE) {\
														current_error = type_error(EXPECTED_TYPE, OPERAND_NAME.type(), ip);\
														goto stop_exec;\
													}\

#define NORMALIZE_ARRAY_INDEX(NUMERICAL_IND, LENGTH)	int32_t index = floor(NUMERICAL_IND.number());\
														if(index >= LENGTH || -index >= LENGTH) {\
															current_error = index_error(NUMERICAL_IND.number(), index, LENGTH, ip);\
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

		//other miscellaneous operations
		case opcode::LOAD_CONSTANT:
			evaluation_stack.push_back(constants[ins.operand]);
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
		case opcode::PUSH_SCRATCHPAD:
			scratchpad_stack.push_back(evaluation_stack.back());
			evaluation_stack.pop_back();
			goto next_ins;
		case opcode::REVERSE_SCRATCHPAD:
			std::reverse(scratchpad_stack.begin(), scratchpad_stack.end());
			goto next_ins;
		case opcode::DUPLICATE:
			evaluation_stack.push_back(evaluation_stack.back());
			goto next_ins;

		//table operations
		case opcode::LOAD_TABLE_ELEM:
		{
			value key_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			LOAD_OPERAND(table_val, vtype::TABLE);

			table_entry& table_entry = table_entries[table_val.table_id()];
			uint64_t hash = key_val.compute_hash();

			auto it = table_entry.hash_to_index.find(hash);
			if (it == table_entry.hash_to_index.end()) {
				evaluation_stack.push_back(value());
			}
			else {
				evaluation_stack.push_back(table_elems[table_entry.table_start + it->second]);
			}
			goto next_ins;
		}
		case opcode::STORE_TABLE_ELEM: {
			value store_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			value key_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			LOAD_OPERAND(table_val, vtype::TABLE);

			table_entry& table_entry = table_entries[table_val.table_id()];
			uint64_t hash = key_val.compute_hash();

			auto it = table_entry.hash_to_index.find(hash);
			if (it == table_entry.hash_to_index.end()) { //add new key to dictionary
				if (table_entry.used_elems == table_entry.allocated_capacity) {
					if (!reallocate_table(table_val.table_id(), 4, 1))
					{
						current_error = error(etype::MEMORY, "Failed to add to table.", ip);
						goto stop_exec;
					}
				}

				table_entry.hash_to_index.insert({ hash, table_entry.used_elems });
				table_elems[table_entry.table_start + table_entry.used_elems] = store_val;
				table_entry.used_elems++;
			}
			else { //write to existing value
				table_elems[table_entry.table_start + it->second] = store_val;
			}

			evaluation_stack.push_back(store_val);
			goto next_ins;
		}
		case opcode::ALLOCATE_DYN:
		{
			LOAD_OPERAND(length_val, vtype::NUMBER);
			
			uint32_t size = (uint32_t)floor(length_val.number());
			std::optional<uint64_t> res = allocate_table(size);
			if (!res.has_value()) {
				std::stringstream ss;
				ss << "Failed to allocate new table with " << length_val.number() << " elements";
				if (size != length_val.number()) {
					ss << "(rounded to " << size << ")";
				}
				ss << '.';
				current_error = error(etype::MEMORY, ss.str(), ip);
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
				current_error = error(etype::MEMORY, ss.str(), ip);
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

		//function operations
		case opcode::FUNCTION:
		{
			uint32_t id = ins.operand;
			uint32_t end_addr = ip;

			loaded_function_entry entry = {
				.start_address = ip + 1,
			};
			do {
				switch (instructions[end_addr].op)
				{
				case opcode::MAKE_CLOSURE:
					entry.referenced_func_ids.insert(instructions[end_addr].operand);
					break;
				case opcode::LOAD_CONSTANT: {
					value& constant = constants[instructions[end_addr].operand];
					if (constant.type() == vtype::STRING)
						entry.referenced_const_strs.insert(constant.str());
					break;
				}
				}

				end_addr++;
				if (end_addr == loaded_instructions.size()) {
					std::stringstream ss;
					ss << "No matching function end instruction for function instruction at " << ip << '.';
					current_error = error(etype::INTERNAL_ERROR, ss.str(), end_addr);
					goto stop_exec;
				}
			} while (instructions[end_addr].op != opcode::FUNCTION_END);

			entry.length = end_addr - ip;
			ip = end_addr;

			instruction end_ins = instructions[ip];
			entry.parameter_count = end_ins.operand;

			function_entries.insert({ id, entry });

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
			LOAD_OPERAND(fn_val, vtype::CLOSURE);
			auto fn_closure = fn_val.closure();

			evaluation_stack.push_back(value(fn_closure.second));

			return_stack.push_back(ip);
			loaded_function_entry& fn_entry = function_entries[fn_closure.first];

			if (fn_entry.parameter_count != ins.operand) { //argument count mismatch
				std::stringstream ss;

				ss << "Function expected " << fn_entry.parameter_count << " argument(s), but got " << ins.operand << " instead.";
				current_error = error(etype::ARGUMENT_COUNT_MISMATCH, ss.str(), ip);
				goto stop_exec;
			}

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
			current_error = error(etype::INTERNAL_ERROR, "Encountered unexpected invalid instruction", ip);
			goto stop_exec;
		default: {
			std::stringstream ss;
			ss << "Unrecognized opcode " << ins.op << " is unhandled.";
			current_error = error(etype::INTERNAL_ERROR, ss.str(), ip);
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
		start_ip = (uint32_t)loaded_instructions.size();
		return current_error.value();
	}
	else {
		garbage_collect(gc_collection_mode::FINALIZE_COLLECT_RETURN);
		start_ip = (uint32_t)loaded_instructions.size();
		assert(evaluation_stack.size() == 1);
		value to_return = evaluation_stack.back();
		evaluation_stack.pop_back();
		return to_return;
	}
#undef LOAD_OPERAND
#undef NORMALIZE_ARRAY_INDEX
}

uint32_t instance::emit_function_start(std::vector<instruction>& instructions) {
	uint32_t id;
	if (available_function_ids.empty())
		id = max_function_id++;
	else {
		id = available_function_ids.front();
		available_function_ids.pop();
	}
	instructions.push_back({ .op = opcode::FUNCTION, .operand = id });
	return id;
}