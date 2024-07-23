#include <cstring>
#include <cassert>
#include "hash.h"
#include "value.h"

using namespace HulaScript::Runtime;

const uint64_t value::compute_hash() {
	uint64_t init_hash = 0;
	switch (_type)
	{
	case vtype::CLOSURE:
		init_hash = hash_combine(func_id, data.table_id);
		break;
	case vtype::TABLE:
		init_hash = data.table_id;
		break;
	case vtype::STRING:
		init_hash = str_hash(data.str);
		break;
	case vtype::NUMBER:
		init_hash = data.table_id;
		break;
	case vtype::NIL:
		return 0;
	}

	return hash_combine(init_hash, (uint64_t)_type);
}