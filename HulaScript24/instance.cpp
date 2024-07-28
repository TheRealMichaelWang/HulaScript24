#include <cstdlib>
#include <cassert>
#include <sstream>
#include "hash.h"
#include "instance.h"

using namespace HulaScript::Runtime;

instance::instance(uint32_t max_locals, uint32_t max_globals, size_t max_table) : 
	max_locals(max_locals), max_globals(max_globals), max_table(max_table),
	local_offset(0), extended_local_offset(0), global_offset(0), table_offset(0), start_ip(0), max_function_id(0), max_table_id(0),
	local_elems((value*)malloc(max_locals * sizeof(value))),
	global_elems((value*)malloc(max_globals * sizeof(value))),
	table_elems((value*)malloc(max_table * sizeof(value)))
{
	assert(local_elems != NULL);
	assert(global_elems != NULL);
	assert(table_elems != NULL);

	active_strs.set_deleted_key(NULL);
}

instance::~instance() {
	for (char* str : active_strs)
		free(str);

	free(local_elems);
	free(global_elems);
	free(table_elems);
}

uint32_t instance::make_string(const char* string) {
	return add_constant(value((char*)string));
}

uint32_t instance::make_string(std::string str) {
	return make_string(str.c_str());
}

uint32_t instance::add_constant(value constant) {
	uint64_t hash = constant.compute_hash();

	auto it = added_constant_hashes.find(hash);
	if (it == added_constant_hashes.end()) {
		uint32_t id;
		if (available_constant_ids.empty())
			id = (uint32_t)constants.size();
		else {
			id = available_constant_ids.front();
			available_constant_ids.pop();
		}

		if (constant.type() == vtype::STRING) {
			char* to_store = strdup(constant.str());
			assert(to_store != NULL);
			active_strs.insert(to_store);
			constant = value(to_store);
		}
		if (id == constants.size())
			constants.push_back(constant);
		else
			constants[id] = constant;

		added_constant_hashes.insert({ hash, id });
		return id;
	}
	return it->second;
}

error instance::type_error(vtype expected, vtype got, std::optional<source_loc> location, uint32_t ip) {
	static const char* type_names[] = {
		"nil",
		"number",
		"string",
		"table",
		"closure"
	};

	std::stringstream ss;
	ss << "Expected type " << type_names[expected] << " but got " << type_names[got] << " instead.";

	return error(etype::UNEXPECTED_TYPE, ss.str(), location, ip);
}