#include <cstring>
#include <cassert>
#include "hash.h"
#include "instance.h"

using namespace HulaScript;

instance::value instance::make_nil() {
	return {
		.type = instance::value::vtype::NIL
	};
}

instance::value instance::make_number(double number) {
	return {
		.type = instance::value::vtype::NUMBER,
		.data = {.number = number }
	};
}

instance::value instance::make_string(const char* string) {
	char* new_ptr = (char*)malloc(strlen(string) * sizeof(char));
	assert(new_ptr != NULL);
	active_strs.insert(new_ptr);
	return {
		.type = instance::value::vtype::STRING,
		.data = {.str = new_ptr}
	};
}

instance::value instance::make_string(std::string str) {
	return make_string(str.c_str());
}

uint32_t instance::add_constant(value constant) {
	uint64_t hash = constant.compute_hash();

	auto it = added_constant_hashes.find(hash);
	if (it == added_constant_hashes.end()) {
		uint32_t id = constants.size();
		constants.push_back(constant);
		added_constant_hashes.insert({ hash, id });
		return id;
	}
	return it->second;
}

instance::value instance::make_bool(bool b) {
	return {
		.type = value::vtype::NUMBER,
		.data = {
			.number = b ? 1.0 : 0.0
		}
	};
}

uint64_t instance::value::compute_hash() {
	uint64_t init_hash = 0;
	switch (type)
	{
	case HulaScript::instance::value::CLOSURE:
		init_hash = hash_combine(func_id, data.table_id);
		break;
	case HulaScript::instance::value::TABLE:
		init_hash = data.table_id;
		break;
	case HulaScript::instance::value::FUNC_PTR:
		init_hash = func_id;
		break;
	case HulaScript::instance::value::STRING:
		init_hash = str_hash(data.str);
		break;
	case HulaScript::instance::value::NUMBER:
		init_hash = data.table_id;
		break;
	case HulaScript::instance::value::NIL:
		return 0;
	}

	return hash_combine(init_hash, (uint64_t)type);
}