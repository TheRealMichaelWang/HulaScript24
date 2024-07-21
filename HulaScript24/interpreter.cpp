#include <cassert>
#include <sstream>
#include <algorithm>
#include "instance.h"

using namespace HulaScript;

std::optional<instance::value> instance::execute(const std::vector<instruction>& new_instructions) {
	std::vector<instruction> total_instructions(loaded_functions.size() + new_instructions.size());

	total_instructions.insert(total_instructions.begin(), loaded_functions.begin(), loaded_functions.end());
	total_instructions.insert(total_instructions.begin() + loaded_functions.size(), new_instructions.begin(), new_instructions.end());

	instruction* instructions = total_instructions.data();
	uint_fast32_t instruction_count = total_instructions.size();

	uint_fast32_t ip = loaded_functions.size();

#define LOAD_OPERAND(OPERAND_NAME, EXPECTED_TYPE)	value OPERAND_NAME = evaluation_stack.back();\
													evaluation_stack.pop_back();\
													if(OPERAND_NAME.type != EXPECTED_TYPE) {\
														type_error(EXPECTED_TYPE, OPERAND_NAME.type, ip);\
														goto error_return;\
													}\

#define NORMALIZE_ARRAY_INDEX(NUMERICAL_IND, LENGTH)	int32_t index = floor(NUMERICAL_IND.data.number);\
														if(index >= LENGTH || -index >= LENGTH) {\
															index_error(NUMERICAL_IND.data.number, index, LENGTH, ip);\
															goto error_return;\
														}\
															
	std::vector<uint32_t> loaded_functions;

	
	while (ip != instruction_count)
	{
		instruction& ins = instructions[ip];

		switch (ins.op)
		{
		//arithmetic operations
		case opcode::ADD: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_number(a.data.number + b.data.number));
			goto next_ins;
		}
		case opcode::SUB: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_number(a.data.number - b.data.number));
			goto next_ins;
		}
		case opcode::MUL: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_number(a.data.number * b.data.number));
			goto next_ins;
		}
		case opcode::DIV: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_number(a.data.number / b.data.number));
			goto next_ins;
		}
		case opcode::MOD: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_number(fmod(a.data.number, b.data.number)));
			goto next_ins;
		}
		case opcode::EXP: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_number(pow(a.data.number, b.data.number)));
			goto next_ins;
		}
		case opcode::LESS: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_bool(a.data.number < b.data.number));
			goto next_ins;
		}
		case opcode::MORE: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_bool(a.data.number > b.data.number));
			goto next_ins;
		}
		case opcode::LESS_EQUAL: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_bool(a.data.number <= b.data.number));
			goto next_ins;
		}
		case opcode::MORE_EQUAL: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_bool(a.data.number >= b.data.number));
			goto next_ins;
		}
		case opcode::EQUALS: {
			value b = evaluation_stack.back();
			evaluation_stack.pop_back();
			value a = evaluation_stack.back();
			evaluation_stack.pop_back();
			evaluation_stack.push_back(make_bool(a.compute_hash() == b.compute_hash()));
			goto next_ins;
		}
		case opcode::NOT_EQUALS: {
			value b = evaluation_stack.back();
			evaluation_stack.pop_back();
			value a = evaluation_stack.back();
			evaluation_stack.pop_back();
			evaluation_stack.push_back(make_bool(a.compute_hash() != b.compute_hash()));
			goto next_ins;
		}
		case opcode::AND: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_bool(a.data.number != 0 && b.data.number != 0));
			goto next_ins;
		}
		case opcode::OR: {
			LOAD_OPERAND(b, value::vtype::NUMBER);
			LOAD_OPERAND(a, value::vtype::NUMBER);
			evaluation_stack.push_back(make_bool(a.data.number != 0 || b.data.number != 0));
			goto next_ins;
		}
		case opcode::NEGATE: {
			value& back = evaluation_stack.back();
			if (back.type != value::vtype::NUMBER) {
				type_error(value::vtype::NUMBER, back.type, ip);
				goto error_return;
			}
			back.data.number = -back.data.number;
			goto next_ins;
		}
		case opcode::NOT: {
			value& back = evaluation_stack.back();
			if (back.type != value::vtype::NUMBER) {
				type_error(value::vtype::NUMBER, back.type, ip);
				goto error_return;
			}
			back.data.number = back.data.number == 0 ? 1 : 0;
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
			evaluation_stack.pop_back();
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
			LOAD_OPERAND(table_val, value::vtype::TABLE);

			table_entry& table_entry = table_entries[table_val.data.table_id];
			uint64_t hash = key_val.compute_hash();

			auto it = table_entry.hash_to_index.find(hash);
			if (it == table_entry.hash_to_index.end()) {
				evaluation_stack.push_back(make_nil());
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
			LOAD_OPERAND(table_val, value::vtype::TABLE);

			table_entry& table_entry = table_entries[table_val.data.table_id];
			uint64_t hash = key_val.compute_hash();

			auto it = table_entry.hash_to_index.find(hash);
			if (it == table_entry.hash_to_index.end()) { //add new key to dictionary
				if (table_entry.used_elems == table_entry.allocated_capacity) {
					if (!reallocate_table(table_val.data.table_id, 4, 1))
					{
						last_error = error(error::etype::MEMORY, "Failed to add to table.", ip);
						goto error_return;
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
			LOAD_OPERAND(length_val, value::vtype::NUMBER);
			
			uint32_t size = floor(length_val.data.number);
			std::optional<uint64_t> res = allocate_table(size);
			if (!res.has_value()) {
				std::stringstream ss;
				ss << "Failed to allocate new table with " << length_val.data.number << " elements";
				if (size != length_val.data.number) {
					ss << "(rounded to " << size << ")";
				}
				ss << '.';
				last_error = error(error::etype::MEMORY, ss.str(), ip);
				goto error_return;
			}

			evaluation_stack.push_back({
				.type = value::vtype::TABLE,
				.data = {.table_id = res.value() }
			});

			goto next_ins;
		}
		case opcode::ALLOCATE_FIXED: {
			std::optional<uint64_t> res = allocate_table(ins.operand);
			if (!res.has_value()) {
				std::stringstream ss;
				ss << "Failed to allocate new array with " << ins.operand << " elements.";
				last_error = error(error::etype::MEMORY, ss.str(), ip);
				goto error_return;
			}
			evaluation_stack.push_back({
				.type = value::vtype::TABLE,
				.data = {.table_id = res.value() }
			});
			goto next_ins;
		}

		//control flow
		case opcode::COND_JUMP_AHEAD:
		{
			LOAD_OPERAND(cond_val, value::vtype::NUMBER);
			if (cond_val.data.number != 0)
				goto next_ins;
		}
		[[fallthrough]];
		case opcode::JUMP_AHEAD:
			ip += ins.operand;
			continue;
		case opcode::COND_JUMP_BACK: //used primarily for do..while
		{
			LOAD_OPERAND(cond_val, value::vtype::NUMBER);
			if (cond_val.data.number == 0)
				goto next_ins;
		}
		[[fallthrough]];
		case opcode::JUMP_BACK:
			ip -= ins.operand;
			continue;

		//function operations
		case opcode::FUNCTION:
		{
			uint32_t id;
			if (available_function_ids.empty())
				id = max_function_id++;
			else {
				id = available_function_ids.front();
				available_function_ids.pop();
			}

			loaded_function_entry entry = {
				.start_address = ip + 1,
				.length = ins.operand
			};
			ip = entry.start_address + entry.length;

			instruction end_ins = instructions[ip];
			assert(end_ins.operand == opcode::FUNCTION_END);

			entry.parameter_count = end_ins.operand;
			function_entries.insert({ id, entry });
			loaded_functions.push_back(id);

			evaluation_stack.push_back({
				.type = value::vtype::FUNC_PTR,
				.func_id = id
			});

			ip++;
			continue;
		}
		case opcode::FUNCTION_END:
			last_error = error(error::etype::INTERNAL_ERROR, "Function end by itself isn't a valid instruction.", ip);
			goto error_return;
		case opcode::FINALIZE_CLOSURE: 
		{
			LOAD_OPERAND(capture_table, value::vtype::TABLE);
			LOAD_OPERAND(func_ptr_val, value::vtype::FUNC_PTR);

			evaluation_stack.push_back({
				.type = value::vtype::CLOSURE,
				.func_id = func_ptr_val.func_id,
				.data = {.table_id = capture_table.data.table_id }
			});
			goto next_ins;
		}
		case opcode::CALL: 
		{
			LOAD_OPERAND(fn_val, value::vtype::CLOSURE);
			evaluation_stack.push_back({
				.type = value::vtype::TABLE,
				.data = {
					.table_id = fn_val.data.table_id
				}
			});

			return_stack.push_back(ip);
			loaded_function_entry& fn_entry = function_entries[fn_val.func_id];

			if (fn_entry.parameter_count != ins.operand) { //argument count mismatch
				std::stringstream ss;

				ss << "Function expected " << fn_entry.parameter_count << " argument(s), but got " << ins.operand << " instead.";
				last_error = error(error::etype::ARGUMENT_COUNT_MISMATCH, ss.str(), ip);
				goto error_return;
			}

			local_offset += extended_local_offset;
			extended_offsets.push_back(extended_local_offset);
			extended_local_offset = 0;
			ip = fn_entry.start_address;
			continue;
		}
		case opcode::RETURN:
			if (return_stack.empty())
				goto value_return;

			ip = return_stack.back();
			return_stack.pop_back();
			extended_local_offset = extended_offsets.back();
			extended_offsets.pop_back();
			local_offset -= extended_local_offset;
			goto next_ins;
		case opcode::INVALID:
			last_error = error(error::etype::INTERNAL_ERROR, "Encountered unexpected invalid instruction", ip);
			goto error_return;
		default: {
			std::stringstream ss;
			ss << "Unrecognized opcode " << ins.op << " is unhandled.";
			last_error = error(error::etype::INTERNAL_ERROR, ss.str(), ip);
			goto error_return;
		}
		}

	next_ins:
		ip++;
	}
	
	if (evaluation_stack.empty())
		evaluation_stack.push_back(make_nil());
	goto value_return;
	
error_return:
	finalize_collect(total_instructions);
	return std::nullopt;
value_return:
	finalize_collect(total_instructions);
	assert(evaluation_stack.size() == 1);
	return evaluation_stack.back();
#undef LOAD_OPERAND
#undef NORMALIZE_ARRAY_INDEX
}