#include <assert.h>
#include <sstream>
#include "instance.h"

using namespace HulaScript;

struct loaded_function_entry {
	uint32_t begin_ip;
	uint32_t length;
	uint32_t global_addr;
};

std::optional<instance::value> instance::execute(const std::vector<instruction>& new_instructions, const std::vector<value>& constants) {
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
		instruction ins = instructions[ip];

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
		case opcode::LOAD_CONSTANT: {
			evaluation_stack.push_back(constants[ins.operand]);
			goto next_ins;
		}
		case opcode::DISCARD_TOP:
		{
			evaluation_stack.pop_back();
			goto next_ins;
		}

		//table operations
		case opcode::LOAD_ARRAY_FIXED:
		{
			LOAD_OPERAND(array_val, value::vtype::ARRAY);

			evaluation_stack.push_back(table_elems[table_entries[array_val.data.table_id].table_start + ins.operand]);
			goto next_ins;
		}
		case opcode::LOAD_ARRAY_ELEM:
		load_array_elem:
		{
			LOAD_OPERAND(index_val, value::vtype::NUMBER);
			LOAD_OPERAND(array_val, value::vtype::ARRAY);

			table_entry array_entry = table_entries[array_val.data.table_id];
			NORMALIZE_ARRAY_INDEX(index_val, array_entry.length);

			evaluation_stack.push_back(table_elems[array_entry.table_start + index]);
			goto next_ins;
		}
		case opcode::LOAD_OBJ_PROP:
		{
			LOAD_OPERAND(obj_val, value::vtype::OBJECT);

			table_entry table_entry = table_entries[obj_val.data.table_id];
			keymap_entry proto_entry = keymap_entries[obj_val.data.table_id];

			auto it = proto_entry.hash_to_index.find(ins.operand);
			if (it == proto_entry.hash_to_index.end())
			{
				last_error = error(error::etype::PROPERTY_NOT_FOUND, ip);
				goto error_return;
			}

			evaluation_stack.push_back(table_elems[table_entry.table_start + it->second]);
			goto next_ins;
		}
		case opcode::LOAD_DICT_ELEM: 
		{
			value key_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			value dict_val = evaluation_stack.back();
			evaluation_stack.pop_back();

			if (dict_val.type == value::vtype::ARRAY) {
				evaluation_stack.push_back(dict_val);
				evaluation_stack.push_back(key_val);
				goto load_array_elem;
			}
			else if (dict_val.type != value::vtype::DICTIONARY) {
				type_error(value::vtype::DICTIONARY, dict_val.type, ip);
				goto error_return;
			}

			table_entry table_entry = table_entries[dict_val.data.table_id];
			keymap_entry proto_entry = keymap_entries[dict_val.data.table_id];
			uint64_t hash = key_val.compute_hash();
			
			auto it = proto_entry.hash_to_index.find(hash);
			if (it == proto_entry.hash_to_index.end()) {
				last_error = error(error::etype::KEY_NOT_FOUND, ip);
				goto error_return;
			}

			evaluation_stack.push_back(table_elems[table_entry.table_start + it->second]);
			goto next_ins;
		}
		case opcode::STORE_ARRAY_FIXED:
		{
			value store_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			LOAD_OPERAND(array_val, value::vtype::ARRAY);
			table_elems[table_entries[array_val.data.table_id].table_start + ins.operand] = store_val;

			evaluation_stack.push_back(store_val);
			goto next_ins;
		}
		case opcode::STORE_ARRAY_ELEM:
		store_array_elem:
		{
			value store_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			LOAD_OPERAND(index_val, value::vtype::NUMBER);
			LOAD_OPERAND(array_val, value::vtype::ARRAY);

			table_entry array_entry = table_entries[array_val.data.table_id];
			NORMALIZE_ARRAY_INDEX(index_val, array_entry.length);

			table_elems[array_entry.table_start + index] = store_val;
			evaluation_stack.push_back(store_val);
			goto next_ins;
		}
		case opcode::STORE_OBJ_PROP: 
		{
			value store_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			LOAD_OPERAND(obj_val, value::vtype::OBJECT);

			table_entry table_entry = table_entries[obj_val.data.table_id];
			auto proto_entry = keymap_entries.find(obj_val.data.table_id);
			assert(proto_entry != keymap_entries.end());
			auto it = proto_entry->second.hash_to_index.find(ins.operand);
			if (it == proto_entry->second.hash_to_index.end())
			{
				if (proto_entry->second.count == table_entry.length) {
					last_error = error(error::etype::MEMORY, "Failed to add property to object.", ip);
					goto error_return;
				}

				proto_entry->second.hash_to_index.insert({ ins.operand, proto_entry->second.count });
				table_elems[table_entry.table_start + table_entry.length] = store_val;
				proto_entry->second.count++;
			}
			else {
				table_elems[table_entry.table_start + it->second] = store_val;
			}

			evaluation_stack.push_back(store_val);
			goto next_ins;
		}
		case opcode::STORE_DICT_ELEM: {
			value store_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			value key_val = evaluation_stack.back();
			evaluation_stack.pop_back();
			value dict_val = evaluation_stack.back();
			evaluation_stack.pop_back();

			if (dict_val.type == value::vtype::ARRAY) {
				evaluation_stack.push_back(dict_val);
				evaluation_stack.push_back(key_val);
				evaluation_stack.push_back(dict_val);
				goto store_array_elem;
			}
			else if (dict_val.type != value::vtype::DICTIONARY) {
				type_error(value::vtype::DICTIONARY, dict_val.type, ip);
				goto error_return;
			}

			table_entry table_entry = table_entries[dict_val.data.table_id];
			auto proto_entry = keymap_entries.find(dict_val.data.table_id);
			assert(proto_entry != keymap_entries.end());
			uint64_t hash = key_val.compute_hash();

			auto it = proto_entry->second.hash_to_index.find(hash);

			if (it == proto_entry->second.hash_to_index.end()) { //add new key to dictionary
				if (proto_entry->second.count == table_entry.length) {
					if (!reallocate_table(dict_val.data.table_id, 4, 1))
					{
						last_error = error(error::etype::MEMORY, "Failed to add to dictionary.", ip);
						goto error_return;
					}
				}

				proto_entry->second.hash_to_index.insert({ hash, proto_entry->second.count });
				table_elems[table_entry.table_start + table_entry.length] = store_val;
				proto_entry->second.count++;
			}
			else { //write to existing value
				table_elems[table_entry.table_start + it->second] = store_val;
			}

			evaluation_stack.push_back(store_val);
			goto next_ins;
		}
		case opcode::ALLOCATE_ARRAY:
		{
			LOAD_OPERAND(length_val, value::vtype::NUMBER);
			
			uint32_t size = floor(length_val.data.number);
			std::optional<uint64_t> res = allocate_table(size);
			if (!res.has_value()) {
				std::stringstream ss;
				ss << "Failed to allocate new array with " << length_val.data.number << " elements";
				if (size != length_val.data.number) {
					ss << "(rounded to " << size << ")";
				}
				ss << '.';
				last_error = error(error::etype::MEMORY, ss.str(), ip);
				goto error_return;
			}

			evaluation_stack.push_back({
				.type = value::vtype::ARRAY,
				.data = {.table_id = res.value() }
			});

			goto next_ins;
		}
		case opcode::ALLOCATE_ARRAY_FIXED: {
			std::optional<uint64_t> res = allocate_table(ins.operand);
			if (!res.has_value()) {
				std::stringstream ss;
				ss << "Failed to allocate new array with " << ins.operand << " elements.";
				last_error = error(error::etype::MEMORY, ss.str(), ip);
				goto error_return;
			}
			evaluation_stack.push_back({
				.type = value::vtype::ARRAY,
				.data = {.table_id = res.value() }
			});
			goto next_ins;
		}
		case opcode::ALLOCATE_OBJ: {
			std::optional<uint64_t> res = allocate_table(ins.operand);
			if (!res.has_value()) {
				std::stringstream ss;
				ss << "Failed to allocate new object with " << ins.operand << " elements.";
				last_error = error(error::etype::MEMORY, ss.str(), ip);
				goto error_return;
			}
			
			evaluation_stack.push_back({
				.type = value::vtype::OBJECT,
				.data = {.table_id = res.value() }
			});
			keymap_entries.insert({ res.value(), {
				.count = 0
			}});
			goto next_ins;
		}
		case opcode::ALLOCATE_DICT: {
			uint32_t size = ins.operand == 0 ? 8 : ins.operand;
			std::optional<uint64_t> res = allocate_table(size);

			if (!res.has_value()) {
				std::stringstream ss;
				ss << "Failed to allocate new dictionary with " << size << " elements.";
				last_error = error(error::etype::MEMORY, ss.str(), ip);
				goto error_return;
			}

			evaluation_stack.push_back({
				.type = value::vtype::DICTIONARY,
				.data = {.table_id = res.value() }
			});
			keymap_entries.insert({ res.value(), {
				.count = 0
			} });
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
		case opcode::COND_JUMP_BACK:
		{
			LOAD_OPERAND(cond_val, value::vtype::NUMBER);
			if (cond_val.data.number != 0)
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
			function_entries.insert({ id, entry });
			loaded_functions.push_back(id);

			evaluation_stack.push_back({
				.type = value::vtype::FUNC_PTR,
				.func_id = id
			});

			ip = entry.start_address + entry.length;
			continue;
		}
		case opcode::MAKE_CLOSURE: 
		{
			std::optional<uint64_t> alloc_res = allocate_table(ins.operand);
			if (!alloc_res.has_value())
			{
				std::stringstream ss;
				ss << "Unable to make closure, failed to allocate capture table with " << ins.operand << " elements.";
				last_error = error(error::etype::MEMORY, ss.str(), ip);
				goto error_return;
			}
			table_entry table_entry = table_entries[alloc_res.value()];

			for (uint_fast32_t i = 0; i < ins.operand; i++) {
				table_elems[table_entry.table_start + i] = evaluation_stack.back();
				evaluation_stack.pop_back();
			}

			LOAD_OPERAND(func_ptr_val, value::vtype::FUNC_PTR);

			evaluation_stack.push_back({
				.type = value::vtype::CLOSURE,
				.func_id = func_ptr_val.func_id,
				.data = {.table_id = alloc_res.value() }
			});
			goto next_ins;
		}
		case opcode::CALL_CLOSURE: 
		{
			LOAD_OPERAND(closure_val, value::vtype::CLOSURE)

			return_stack.push_back(ip);
			table_entry closure_entry = table_entries[closure_val.data.table_id];

			for (uint_fast32_t i = 0; i < closure_entry.length; i++) {
				evaluation_stack.push_back(table_elems[closure_entry.table_start + i]);
			}

			ip = function_entries[closure_val.data.table_id].start_address;
			continue;
		}
		case opcode::RETURN:
			if (return_stack.empty())
				goto value_return;

			ip = return_stack.back();
			return_stack.pop_back();
			continue;
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