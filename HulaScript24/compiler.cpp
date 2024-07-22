#include <sstream>
#include <cmath>
#include <cassert>
#include "hash.h"
#include "compiler.h"

using namespace HulaScript;

compiler::compiler() : max_globals(0) {
	scope_stack.push_back({ });
}

#define UNWRAP_RES_AND_HANDLE(RESNAME, RES, HANDLE) auto RESNAME = RES; if(std::holds_alternative<error>(RESNAME)) { HANDLE; return std::get<error>(RESNAME); }

#define UNWRAP_RES(RESNAME, RES) UNWRAP_RES_AND_HANDLE(RESNAME, RES, {});

#define UNWRAP_AND_HANDLE(RES, HANDLER) { auto opt_res = RES; if(opt_res.has_value()) { HANDLER; return opt_res.value(); }}

#define UNWRAP(RES) UNWRAP_AND_HANDLE(RES, {})

#define SCAN_AND_HANDLE(HANDLER) {UNWRAP_RES_AND_HANDLE(scan_res, tokenizer.scan_token(), HANDLER)};

#define SCAN SCAN_AND_HANDLE({})

#define MATCH_AND_HANDLE(EXPECTED_TOK, HANDLER) { auto match_res = tokenizer.match(EXPECTED_TOK);\
								if(match_res.has_value()) {\
									HANDLER;\
									return match_res.value();\
								}}

#define MATCH(EXPECTED_TOK) MATCH_AND_HANDLE(EXPECTED_TOK, {})

#define MATCH_AND_SCAN_AND_HANDLE(EXPECTED_TOK, HANDLER) { auto match_res = tokenizer.match(EXPECTED_TOK);\
						if(match_res.has_value()) {\
							HANDLER;\
							return match_res.value();\
						}\
						SCAN_AND_HANDLE(HANDLER) }

#define MATCH_AND_SCAN(EXPECTED_TOK) MATCH_AND_SCAN_AND_HANDLE(EXPECTED_TOK, {})

#define IF_AND_SCAN(TOK) if(tokenizer.last_tok().type == TOK) { SCAN }

std::optional<compiler::error> compiler::compile_value(compiler::tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& current_section, std::vector<instance::instruction>& function_section, bool expects_statement) {
	tokenizer::token& token = tokenizer.last_token();
	source_loc& begin_loc = tokenizer.last_token_loc();
	switch (token.type)
	{
	case tokenizer::token_type::IDENTIFIER: {
		std::string id = tokenizer.last_token().str();
		SCAN;

		auto local_it = active_variables.find(id);
		if (local_it != active_variables.end()) {
			if (tokenizer.match_last(tokenizer::SET)) {
				SCAN;
				UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));

				if (!local_it->second.is_global && local_it->second.func_id < func_decl_stack.size() - 1) {
					std::stringstream ss;
					ss << "Cannot set captured variable " << id << ", which was declared in function " << func_decl_stack[local_it->second.func_id].name << ", not the current function " << func_decl_stack.back().name << '.';
					return error(error::etype::CANNOT_SET_CAPTURED, ss.str(), begin_loc);
				}
				
				current_section.push_back({ .op = local_it->second.is_global ? instance::opcode::STORE_GLOBAL : instance::opcode::STORE_LOCAL, .operand = local_it->second.local_id });
				return std::nullopt;
			}
			else {
				if (!local_it->second.is_global && local_it->second.func_id < func_decl_stack.size() - 1) {
					function_declaration& current_func = func_decl_stack.back();

					for (int32_t i = func_decl_stack.size() - 1; i > local_it->second.func_id; i--) {
						auto capture_it = func_decl_stack[i].captured_vars.find(id);
						if (capture_it == func_decl_stack[i].captured_vars.end()) {
							func_decl_stack[i].captured_vars.insert(id);
						}
					}

					current_section.push_back({ .op = instance::opcode::LOAD_LOCAL, .operand = 0 }); //load capture table, which is always local variable 0 in functions
					current_section.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_string(id)) });
					current_section.push_back({ .op = instance::opcode::LOAD_TABLE_ELEM });
				}
				else {
					current_section.push_back({ .op = local_it->second.is_global ? instance::opcode::LOAD_GLOBAL : instance::opcode::LOAD_LOCAL, .operand = local_it->second.local_id });
				}
				break;
			}
		}
		else {
			auto class_it = class_decls.find(id);
			if (class_it != class_decls.end()) {
				return std::nullopt;
			}
			else if (tokenizer.match_last(tokenizer::SET) && expects_statement) { //declare variable here
				SCAN;
				UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));

				scope_stack.back().symbol_names.push_back(id);
				variable_symbol sym = {
					.name = id,
					.is_global = false,
					.local_id = func_decl_stack.back().max_locals,
					.func_id = (uint32_t)(func_decl_stack.size() - 1)
				};
				active_variables.insert({ id, sym });
				func_decl_stack.back().max_locals++;
					
				current_section.push_back({ .op = instance::opcode::STORE_LOCAL, .operand = sym.local_id });
				return std::nullopt;
			}
			else {
				std::stringstream ss;
				ss << "Symbol " << id << " does not exist.";
				return error(error::etype::SYMBOL_NOT_FOUND, ss.str(), begin_loc);
			}
		}
	}
	case tokenizer::token_type::MINUS:
		SCAN;
		UNWRAP(compile_value(tokenizer, instance, current_section, function_section, false));
		current_section.push_back({ .op = instance::opcode::NEGATE });
		break;
	case tokenizer::token_type::NOT:
		SCAN;
		UNWRAP(compile_value(tokenizer, instance, current_section, function_section, false));
		current_section.push_back({ .op = instance::opcode::NOT });
		break;
	case tokenizer::token_type::NUMBER:
		current_section.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_number(token.number())) });
		SCAN;
		break;
	case tokenizer::token_type::STRING_LITERAL:
		current_section.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_string(token.str())) });
		SCAN;
		break;
	case tokenizer::token_type::TABLE:
		SCAN;
		MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACKET);
		if (tokenizer.match_last(tokenizer::token_type::NUMBER)) {
			uint32_t length = floor(tokenizer.last_token().number());
			current_section.push_back({ .op = instance::opcode::ALLOCATE_FIXED, .operand = length });
			SCAN;
		}
		else {
			UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
			current_section.push_back({ .op = instance::opcode::ALLOCATE_DYN });
		}
		MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACKET);
		break;
	case tokenizer::token_type::OPEN_BRACKET: {
		SCAN;
		uint32_t length = 0;
		while (!tokenizer.match_last(tokenizer::token_type::CLOSE_BRACKET) && !tokenizer.match_last(tokenizer::token_type::END_OF_SOURCE))
		{
			if (length > 0) {
				MATCH_AND_SCAN(tokenizer::token_type::COMMA);
			}
			UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
			current_section.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			length++;
		}
		MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACKET);

		current_section.push_back({ .op = instance::opcode::ALLOCATE_FIXED, .operand = length });
		for (uint_fast32_t i = length; i >= 1; i--) {
			current_section.push_back({ .op = instance::opcode::DUPLICATE });
			current_section.push_back({ .op = instance::opcode::POP_SCRATCHPAD });

			instance::value val = instance.make_number(i);
			current_section.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(val) });
			current_section.push_back({ .op = instance::opcode::STORE_TABLE_ELEM });
			current_section.push_back({ .op = instance::opcode::DISCARD_TOP });
		}
		break;
	}
	case tokenizer::token_type::OPEN_BRACE: {
		SCAN;
		uint32_t length = 0;
		while (!tokenizer.match_last(tokenizer::token_type::CLOSE_BRACE) && !tokenizer.match_last(tokenizer::token_type::END_OF_SOURCE))
		{
			if (length > 0) {
				MATCH_AND_SCAN(tokenizer::token_type::COMMA);
			}
			MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACE);
			UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
			current_section.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			MATCH_AND_SCAN(tokenizer::token_type::COMMA);
			UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
			current_section.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACE);
			length++;
		}
		MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACE);

		current_section.push_back({ .op = instance::opcode::REVERSE_SCRATCHPAD });
		current_section.push_back({ .op = instance::opcode::ALLOCATE_FIXED, .operand = length });
		for (uint_fast32_t i = 0; i < length; i++) {
			current_section.push_back({ .op = instance::opcode::DUPLICATE });
			current_section.push_back({ .op = instance::opcode::POP_SCRATCHPAD }); //pop key
			current_section.push_back({ .op = instance::opcode::POP_SCRATCHPAD }); //pop value
			current_section.push_back({ .op = instance::opcode::STORE_TABLE_ELEM });
			current_section.push_back({ .op = instance::opcode::DISCARD_TOP });
		}
		break;
	}
	case tokenizer::token_type::OPEN_PAREN:
		SCAN;
		UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
		MATCH_AND_SCAN(tokenizer::token_type::CLOSE_PAREN);
		break;
	case tokenizer::token_type::FUNCTION: {
		SCAN;
		std::stringstream ss;
		ss << "Anonymous function literal in " << func_decl_stack.back().name;
		UNWRAP(compile_function(ss.str(), tokenizer, instance, current_section, function_section));
		break;
	}
	default:
		return tokenizer.make_unexpected_tok_err(std::nullopt);
	}

	bool is_statement = false;
	for (;;) {
		token = tokenizer.last_token();
		switch (token.type)
		{
		case tokenizer::token_type::PERIOD:
		{
			SCAN;
			MATCH(tokenizer::token_type::IDENTIFIER);
			current_section.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_string(tokenizer.last_token().str())) });
			SCAN;

			if (tokenizer.match_last(tokenizer::token_type::SET)) {
				SCAN;
				UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
				current_section.push_back({ .op = instance::opcode::STORE_TABLE_ELEM });
				return std::nullopt;
			}
			else {
				current_section.push_back({ .op = instance::opcode::LOAD_TABLE_ELEM });
				is_statement = false;
			}
			break;
		}
		case tokenizer::token_type::OPEN_BRACKET: {
			SCAN;
			UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
			MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACKET);

			if (tokenizer.match_last(tokenizer::token_type::SET)) {
				SCAN;
				UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
				current_section.push_back({ .op = instance::opcode::STORE_TABLE_ELEM });
				return std::nullopt;
			}
			else {
				current_section.push_back({ .op = instance::opcode::LOAD_TABLE_ELEM });
				is_statement = false;
			}
			break;
		}
		case tokenizer::token_type::OPEN_PAREN: {
			SCAN;

			current_section.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			uint32_t length = 0;
			while (!tokenizer.match_last(tokenizer::token_type::CLOSE_PAREN) && !tokenizer.match_last(tokenizer::token_type::END_OF_SOURCE))
			{
				if (length > 0) {
					MATCH_AND_SCAN(tokenizer::token_type::COMMA);
				}
				UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
			}
			MATCH_AND_SCAN(tokenizer::token_type::CLOSE_PAREN);

			current_section.push_back({ .op = instance::opcode::POP_SCRATCHPAD });
			current_section.push_back({ .op = instance::opcode::CALL, .operand = length });
			is_statement = true;
			break;
		}
		case tokenizer::token_type::QUESTION:
		{
			SCAN;
			uint32_t cond_ins_ip = current_section.size();
			current_section.push_back({ .op = instance::opcode::COND_JUMP_AHEAD });

			UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0)); //if true value
			uint32_t jump_to_end_ip = current_section.size();
			current_section.push_back({ .op = instance::opcode::JUMP_AHEAD });

			MATCH_AND_SCAN(tokenizer::token_type::COLON);
			current_section[cond_ins_ip].operand = current_section.size() - cond_ins_ip;
			UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0)); //if false value
			current_section[jump_to_end_ip].operand = current_section.size() - jump_to_end_ip;
			is_statement = false;

			goto return_immediatley;
		}
		default:
		return_immediatley:
			if (expects_statement && !is_statement) {
				return error(error::etype::UNEXPECTED_VALUE, "Expected statement, but got value instead.", begin_loc);
			}
			return std::nullopt;
		}
	}
}

std::optional<compiler::error> compiler::compile_expression(tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& current_section, std::vector<instance::instruction>& function_section, int min_prec) {
	static int min_precs[] = {
		5, //plus,
		5, //minus
		6, //multiply
		6, //divide
		6, //modulo
		7, //exponentiate

		3, //less
		3, //more
		3, //less eq
		3, //more eq
		3, //eq
		3, //not eq

		1, //and
		1 //or
	};

	UNWRAP(compile_value(tokenizer, instance, current_section, function_section, false)); //lhs

	while (tokenizer.last_token().type >= tokenizer::token_type::PLUS && tokenizer.last_token().type <= tokenizer::token_type::AND && min_precs[tokenizer.last_token().type - tokenizer::token_type::PLUS] > min_prec)
	{
		SCAN;
		UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, min_precs[tokenizer.last_token().type - tokenizer::token_type::PLUS]));
		current_section.push_back({ .op = (instance::opcode)((tokenizer.last_token().type - tokenizer::token_type::PLUS) + instance::opcode::ADD) });
	}

	return std::nullopt;
}

std::optional<compiler::error> compiler::compile_statement(tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& current_section, std::vector<instance::instruction>& function_section) {
	tokenizer::token& token = tokenizer.last_token();
	source_loc& begin_loc = tokenizer.last_token_loc();

	switch (token.type)
	{
	case tokenizer::token_type::WHILE: {
		SCAN;
		loop_stack.push_back({});
		uint32_t loop_begin_ip = current_section.size();
		UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
		MATCH_AND_SCAN(tokenizer::token_type::DO);
		uint32_t cond_jump_ip = current_section.size();
		current_section.push_back({ .op = instance::opcode::COND_JUMP_AHEAD });
		UNWRAP(compile_block(tokenizer, instance, current_section, function_section, [](tokenizer::token_type t) -> bool { return t == tokenizer::token_type::END_BLOCK; }));
		MATCH_AND_SCAN(tokenizer::token_type::END_BLOCK);
		current_section.push_back({ .op = instance::opcode::JUMP_BACK, .operand = (uint32_t)(current_section.size() - loop_begin_ip)});
		current_section[cond_jump_ip].operand = current_section.size() - cond_jump_ip;
		unwind_loop(loop_begin_ip, current_section.size(), current_section);

		return std::nullopt;
	}
	case tokenizer::token_type::DO: {
		SCAN;
		loop_stack.push_back({});
		uint32_t loop_begin_ip = current_section.size();
		UNWRAP_AND_HANDLE(compile_block(tokenizer, instance, current_section, function_section, [](tokenizer::token_type t) -> bool { return t == tokenizer::token_type::WHILE; }), loop_stack.pop_back());
		MATCH_AND_SCAN_AND_HANDLE(tokenizer::token_type::WHILE, loop_stack.pop_back());
		uint32_t continue_ip = current_section.size();
		UNWRAP_AND_HANDLE(compile_expression(tokenizer, instance, current_section, function_section, 0), loop_stack.pop_back());
		current_section.push_back({ .op = instance::opcode::JUMP_BACK, .operand = (uint32_t)(current_section.size() - loop_begin_ip) });
		unwind_loop(continue_ip, current_section.size(), current_section);
		return std::nullopt;
	}
	case tokenizer::token_type::IF: {
		SCAN;
		uint32_t last_cond_check_ip = 0;
		std::vector<uint32_t> jump_end_ips;
		do {
			if (jump_end_ips.size() > 0) {
				SCAN;
				current_section[last_cond_check_ip].operand = (current_section.size() - last_cond_check_ip);
			}

			UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0)); //compile condition
			MATCH_AND_SCAN(tokenizer::token_type::THEN);
			last_cond_check_ip = current_section.size();
			current_section.push_back({ .op = instance::opcode::COND_JUMP_AHEAD });

			UNWRAP(compile_block(tokenizer, instance, current_section, function_section, [](tokenizer::token_type t) -> bool { return t == tokenizer::token_type::END_BLOCK || t == tokenizer::token_type::ELIF || t == tokenizer::token_type::ELSE; }));
			jump_end_ips.push_back(current_section.size());
			current_section.push_back({ .op = instance::opcode::JUMP_AHEAD });
		} while (tokenizer.match_last(tokenizer::token_type::ELIF));

		if (tokenizer.match_last(tokenizer::token_type::ELSE)) {
			SCAN;
			current_section[last_cond_check_ip].operand = (current_section.size() - last_cond_check_ip);
			UNWRAP(compile_block(tokenizer, instance, current_section, function_section, [](tokenizer::token_type t) -> bool { return t == tokenizer::token_type::END_BLOCK; }));
		}
		else {
			SCAN;
			jump_end_ips.pop_back();
		}

		for (uint32_t ip : jump_end_ips) {
			current_section[ip].operand = current_section.size() - ip;
		}
		return std::nullopt; 
	}
	case tokenizer::token_type::RETURN: {
		if (func_decl_stack.size() == 0) {
			return error(error::etype::UNEXPECTED_STATEMENT, "Unexpected return statement outside of function", begin_loc);
		}

		SCAN;
		UNWRAP(compile_expression(tokenizer, instance, current_section, function_section, 0));
		current_section.push_back({ .op = instance::opcode::RETURN });
		return std::nullopt;
	}
	case tokenizer::token_type::LOOP_BREAK:
	{
		if (loop_stack.size() == 0) {
			return error(error::etype::UNEXPECTED_STATEMENT, "Unexpected break statement outside of loop.", begin_loc);
		}

		loop_stack.back().break_requests.push_back(current_section.size());
		current_section.push_back({ .op = instance::opcode::INVALID });
		return std::nullopt;
	}
	case tokenizer::token_type::LOOP_CONTINUE: {
		if (loop_stack.size() == 0) {
			return error(error::etype::UNEXPECTED_STATEMENT, "Unexpected continue statement outside of loop.", begin_loc);
		}

		loop_stack.back().continue_requests.push_back(current_section.size());
		current_section.push_back({ .op = instance::opcode::INVALID });
		return std::nullopt;
	}
	default:
		UNWRAP(compile_value(tokenizer, instance, current_section, function_section, true));
		current_section.push_back({ .op = instance::opcode::DISCARD_TOP });
		return std::nullopt;
	}
};

std::optional<compiler::error> compiler::compile_function(std::string name, tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& current_section, std::vector<instance::instruction>& function_section) {
	std::vector<std::string> param_ids;
	MATCH_AND_SCAN(tokenizer::token_type::OPEN_PAREN); 
	while (!tokenizer.match_last(tokenizer::token_type::CLOSE_PAREN) && !tokenizer.match_last(tokenizer::token_type::END_OF_SOURCE))
	{
		if (param_ids.size() > 0) {
			MATCH_AND_SCAN(tokenizer::token_type::COMMA);
		}
		MATCH(tokenizer::token_type::IDENTIFIER);
		std::string id = tokenizer.last_token().str();
		param_ids.push_back(id);
		UNWRAP(validate_symbol_availability(id, "function parameter", tokenizer.last_token_loc()));
		SCAN;
	}
	MATCH_AND_SCAN(tokenizer::token_type::CLOSE_PAREN);

	func_decl_stack.push_back({
		.name = name,
		.max_locals = (uint32_t)(1 + param_ids.size()),
	});
	scope_stack.push_back({ .symbol_names = param_ids });

	std::vector<instance::instruction> func_instructions;
	uint32_t func_id = instance.emit_function_start(func_instructions);

	func_instructions.push_back({ .op = instance::opcode::DECL_LOCAL, .operand = 0 });
	uint32_t param_id = 1;
	for (int_fast32_t i = param_ids.size() - 1; i >= 0; i--) {
		
		variable_symbol sym = {
			.name = param_ids[i],
			.is_global = false,
			.local_id = param_id,
			.func_id = (uint32_t)(func_decl_stack.size() - 1)
		};
		active_variables.insert({ param_ids[i], sym });
	}

	while (!tokenizer.match_last(tokenizer::token_type::END_BLOCK) && !tokenizer.match_last(tokenizer::token_type::END_OF_SOURCE))
	{
		UNWRAP_AND_HANDLE(compile_statement(tokenizer, instance, func_instructions, function_section), {
			unwind_locals(func_instructions, false);
			func_decl_stack.pop_back();
		});
	}
	MATCH_AND_SCAN_AND_HANDLE(tokenizer::token_type::END_BLOCK, {
		unwind_locals(func_instructions, false);
		func_decl_stack.pop_back();
	});
	unwind_locals(func_instructions, false);
	function_section.insert(function_section.begin() + function_section.size(), func_instructions.begin(), func_instructions.end());

	{
		func_instructions[0].operand = (uint32_t)func_instructions.size();
		func_instructions.push_back({ .op = instance::opcode::FUNCTION_END, .operand = (uint32_t)param_ids.size() });
		
		uint32_t capture_size = func_decl_stack.back().captured_vars.size();
		current_section.push_back({ .op = instance::opcode::ALLOCATE_FIXED, .operand = capture_size });

		for (auto captured_var : func_decl_stack.back().captured_vars) {
			current_section.push_back({ .op = instance::opcode::DUPLICATE });
			
			auto var_it = active_variables.find(captured_var);
			uint32_t prop_str_id = instance.add_constant(instance.make_string(captured_var));
			if (var_it->second.func_id < func_decl_stack.size() - 1) { //this is a captured 
				current_section.push_back({ .op = instance::opcode::LOAD_LOCAL, .operand = 0 }); //load capture table
				current_section.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = prop_str_id});
				current_section.push_back({ .op = instance::opcode::LOAD_TABLE_ELEM });
			}
			else { //load local 
				current_section.push_back({ .op = instance::opcode::LOAD_LOCAL, .operand = var_it->second.local_id });
			}

			current_section.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = prop_str_id });
			current_section.push_back({ .op = instance::opcode::STORE_TABLE_ELEM });
			current_section.push_back({ .op = instance::opcode::DISCARD_TOP });
		}

		current_section.push_back({ .op = instance::opcode::MAKE_CLOSURE, .operand = func_id });
	}
	func_decl_stack.pop_back();
	return std::nullopt;
}

std::optional<compiler::error> compiler::compile_top_level(tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& repl_section, std::vector<instance::instruction>& function_section) {
	tokenizer::token& token = tokenizer.last_token();
	source_loc& begin_loc = tokenizer.last_token_loc();

	switch (token.type)
	{
	case tokenizer::token_type::GLOBAL: {
		SCAN;
		MATCH(tokenizer::token_type::IDENTIFIER);
		std::string id = token.str();
		UNWRAP(validate_symbol_availability(id, "global variable", begin_loc));
		SCAN;
		MATCH_AND_SCAN(tokenizer::token_type::SET);

		UNWRAP(compile_expression(tokenizer, instance, repl_section, function_section, 0));

		variable_symbol sym = {
			.name = id,
			.is_global = true,
			.local_id = max_globals
		};
		active_variables.insert({ id, sym });
		max_globals++;

		repl_section.push_back({ .op = instance::opcode::DECL_GLOBAL, .operand = sym.local_id });
		return std::nullopt;
	}
	case tokenizer::token_type::FUNCTION: {
		SCAN;
		MATCH(tokenizer::token_type::IDENTIFIER);
		std::string id = token.str();
		UNWRAP(validate_symbol_availability(id, "top-level function", begin_loc));
		SCAN;

		variable_symbol sym = {
			.name = id,
			.is_global = true,
			.local_id = max_globals
		};
		active_variables.insert({ id, sym });
		max_globals++;

		UNWRAP(compile_function(id, tokenizer, instance, repl_section, function_section));
		repl_section.push_back({ .op = instance::opcode::DECL_GLOBAL, .operand = sym.local_id });

		return std::nullopt;
	}
	default:
		return compile_statement(tokenizer, instance, repl_section, function_section);
	}
}

std::optional<compiler::error> compiler::compile_block(tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& current_section, std::vector<instance::instruction>& function_section, bool(*stop_cond)(tokenizer::token_type)) {
	std::optional<compiler::error> to_return = std::nullopt;

	scope_stack.push_back({ }); //push empty lexical scope
	while (!stop_cond(tokenizer.last_token().type) && !tokenizer.match_last(tokenizer::token_type::END_OF_SOURCE))
	{
		to_return = compile_statement(tokenizer, instance, current_section, function_section);
		if (to_return.has_value())
			goto unwind_and_return;
	}
	
unwind_and_return:
	unwind_locals(current_section, true);
	return to_return;
}

void compiler::unwind_locals(std::vector<instance::instruction>& instructions, bool use_unwind_ins) {
	if (use_unwind_ins) {
		instructions.push_back({ .op = instance::opcode::UNWIND_LOCALS, .operand = (uint32_t)scope_stack.back().symbol_names.size() });
	}
	for (std::string symbol : scope_stack.back().symbol_names) {
		active_variables.erase(symbol);
	}
	scope_stack.pop_back();
}

void compiler::unwind_loop(uint32_t cond_check_ip, uint32_t finish_ip, std::vector<instance::instruction>& instructions) {
	for (uint32_t ip : loop_stack.back().break_requests) {
		assert(ip > finish_ip);
		instructions[ip] = {
			.op = instance::opcode::JUMP_AHEAD,
			.operand = finish_ip - ip
		};
	}

	for (uint32_t ip : loop_stack.back().continue_requests) {
		if (ip > finish_ip) {
			instructions[ip] = {
				.op = instance::opcode::JUMP_AHEAD,
				.operand = finish_ip - ip
			};
		}
		else {
			assert(ip != finish_ip);
			instructions[ip] = {
				.op = instance::opcode::JUMP_BACK,
				.operand = ip - finish_ip
			};
		}
	}
}

std::optional<compiler::error> compiler::validate_symbol_availability(std::string id, std::string symbol_type, source_loc loc) {
	if (class_decls.contains(id)) {
		std::stringstream ss;
		ss << "Cannot declare " << symbol_type << ": a class named " << id << " already exists.";
		return error(error::etype::SYMBOL_ALREADY_EXISTS, ss.str(), loc);
	}
	else if (active_variables.contains(id)) {
		std::stringstream ss;
		ss << "Cannot declare " << symbol_type << ": a variable named " << id << " already exists.";
		return error(error::etype::SYMBOL_ALREADY_EXISTS, ss.str(), loc);
	}
	return std::nullopt;
}