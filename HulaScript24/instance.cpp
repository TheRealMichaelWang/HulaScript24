#include <cstdlib>
#include <cassert>
#include "instance.h"

using namespace HulaScript;

instance::instance(uint32_t max_locals, uint32_t max_globals, size_t max_table) : 
	max_locals(max_locals), max_globals(max_globals), max_table(max_table),
	local_offset(0), extended_local_offset(0), global_offset(0), table_offset(0),
	max_table_id(0), max_function_id(0),
	last_error(),
	local_elems((value*)malloc(max_locals * sizeof(value))),
	global_elems((value*)malloc(max_globals * sizeof(value))),
	table_elems((value*)malloc(max_table * sizeof(value))),
	free_tables([](free_table_entry a, free_table_entry b) -> bool { return a.allocated_capacity < b.allocated_capacity; })
{
	assert(local_elems != NULL);
	assert(global_elems != NULL);
	assert(table_elems != NULL);
}

instance::~instance() {
	for (char* str : active_strs)
		free(str);

	free(local_elems);
	free(global_elems);
	free(table_elems);
}