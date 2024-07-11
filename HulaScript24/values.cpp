#include <cstring>
#include <cassert>
#include "instance.h"

using namespace HulaScript;

uint64_t dj2b_hash(char* str)
{
	uint64_t hash = 5381;
	int c;

	while (c = *str++)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

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

uint64_t instance::value::compute_hash() {
	switch (type)
	{
	case HulaScript::instance::value::DICTIONARY:
		[[fallthrough]];
	case HulaScript::instance::value::CLOSURE:
		[[fallthrough]];
	case HulaScript::instance::value::OBJECT:
		[[fallthrough]];
	case HulaScript::instance::value::ARRAY:
		return data.table_id + 1;
	case HulaScript::instance::value::FUNC_PTR:
		return func_id + 1;
	case HulaScript::instance::value::STRING:
		return dj2b_hash(data.str) + 1;
	case HulaScript::instance::value::NUMBER:
		return data.table_id + 1;
	case HulaScript::instance::value::NIL:
		return 0;
	}
}