#pragma once
#include <cstdint>

namespace HulaScript::Runtime {
	enum opcode {
		//arithmetic op codes
		ADD,
		SUB,
		MUL,
		DIV,
		MOD,
		EXP,

		//comparison operators
		LESS,
		MORE,
		LESS_EQUAL,
		MORE_EQUAL,
		EQUALS,
		NOT_EQUALS,

		//logical operators,
		AND,
		OR,

		//unary operators
		NEGATE,
		NOT,

		//variable load/store
		LOAD_LOCAL,
		LOAD_GLOBAL,
		STORE_LOCAL,
		STORE_GLOBAL,
		DECL_LOCAL,
		DECL_GLOBAL,
		UNWIND_LOCALS,
		PROBE_LOCALS,
		PROBE_GLOBALS,

		//other miscellaneous operations
		LOAD_CONSTANT,
		PUSH_NIL,
		DISCARD_TOP,
		PUSH_SCRATCHPAD,
		POP_SCRATCHPAD,
		REVERSE_SCRATCHPAD,
		DUPLICATE,

		//table operations
		LOAD_TABLE_ELEM,
		STORE_TABLE_ELEM,
		ALLOCATE_DYN,
		ALLOCATE_FIXED,
		ALLOCATE_LITERAL,

		//control flow
		COND_JUMP_AHEAD,
		JUMP_AHEAD,
		COND_JUMP_BACK,
		JUMP_BACK,
		IF_NIL_JUMP_AHEAD,

		//function 
		FUNCTION,
		FUNCTION_END,
		MAKE_CLOSURE,
		CALL,
		CALL_NO_CAPUTRE_TABLE,
		RETURN,

		//invalid
		INVALID
	};
	
	struct instruction {
		opcode op;
		uint32_t operand;
	};
}