#include <sstream>
#include <cmath>
#include "hash.h"
#include "compiler.h"

using namespace HulaScript;

compiler::compiler() : max_globals(0) {
	scope_stack.push_back({ });
}

#define UNWRAP_RES(RESNAME, RES) auto RESNAME = RES; if(std::holds_alternative<error>(RESNAME)) { return std::get<error>(RESNAME); }

#define UNWRAP(RES) { auto opt_res = RES; if(opt_res.has_value()) { return opt_res.value(); }}

#define SCAN {UNWRAP_RES(scan_res, tokenizer.scan_token())};

#define MATCH(EXPECTED_TOK) { auto match_res = tokenizer.match(EXPECTED_TOK);\
								if(match_res.has_value()) {\
									return match_res.value();\
								}}

#define MATCH_AND_SCAN(EXPECTED_TOK) { auto match_res = tokenizer.match(EXPECTED_TOK);\
						if(match_res.has_value()) {\
							return match_res.value();\
						}\
						SCAN }

#define IF_AND_SCAN(TOK) if(tokenizer.last_tok().type == TOK) { SCAN }

std::optional<compiler::error> compiler::compile_value(compiler::tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& instructions, bool expects_statement) {
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
				UNWRAP(compile_expression(tokenizer, instance, instructions, 0));

				if (!local_it->second.is_global && local_it->second.func_id < func_decl_stack.size() - 1) {
					std::stringstream ss;
					ss << "Cannot set captured variable " << id << ", which was declared in function " << func_decl_stack[local_it->second.func_id].name << ", not the current function " << func_decl_stack.back().name << '.';
					return error(error::etype::CANNOT_SET_CAPTURED, ss.str(), begin_loc);
				}
				
				instructions.push_back({ .op = local_it->second.is_global ? instance::opcode::STORE_GLOBAL : instance::opcode::STORE_LOCAL, .operand = local_it->second.local_id });
				return;
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

					instructions.push_back({ .op = instance::opcode::LOAD_LOCAL, .operand = current_func.param_count }); //load capture table
					instructions.push_back({ .op = instance::opcode::LOAD_TABLE_PROP, .operand = str_hash(id.c_str()) });
				}
				else {
					instructions.push_back({ .op = local_it->second.is_global ? instance::opcode::LOAD_GLOBAL : instance::opcode::LOAD_LOCAL, .operand = local_it->second.local_id });
				}
			}
		}
		else {
			auto class_it = class_decls.find(id);
			if (class_it != class_decls.end()) {

			}
			else if (tokenizer.match_last(tokenizer::SET) && expects_statement) { //declare variable here
				SCAN;
				UNWRAP(compile_expression(tokenizer, instance, instructions, 0));

				scope_stack.back().symbol_names.push_back(id);
				variable_symbol sym = {
					.name = id,
					.is_global = false,
					.local_id = func_decl_stack.back().max_locals,
					.func_id = func_decl_stack.size() - 1
				};
				active_variables.insert({ id, sym });
				func_decl_stack.back().max_locals++;
					
				instructions.push_back({ .op = instance::opcode::STORE_LOCAL, .operand = sym.local_id });
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
		UNWRAP(compile_value(tokenizer, instance, instructions, false));
		instructions.push_back({ .op = instance::opcode::NEGATE });
		break;
	case tokenizer::token_type::NOT:
		SCAN;
		UNWRAP(compile_value(tokenizer, instance, instructions, false));
		instructions.push_back({ .op = instance::opcode::NOT });
		break;
	case tokenizer::token_type::NUMBER:
		instructions.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_number(token.number())) });
		SCAN;
		break;
	case tokenizer::token_type::STRING_LITERAL:
		instructions.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_string(token.str())) });
		SCAN;
		break;
	case tokenizer::token_type::TABLE:
		SCAN;
		MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACKET);
		if (tokenizer.match_last(tokenizer::token_type::NUMBER)) {
			uint32_t length = floor(tokenizer.last_token().number());
			instructions.push_back({ .op = instance::opcode::ALLOCATE_FIXED, .operand = length });
			SCAN;
		}
		else {
			UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
			instructions.push_back({ .op = instance::opcode::ALLOCATE_DYN });
		}
		MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACKET);
		break;
	case tokenizer::token_type::OPEN_BRACKET: {
		SCAN;
		uint32_t length = 0;
		while (tokenizer.match_last(tokenizer::token_type::CLOSE_BRACKET))
		{
			if (length > 0) {
				MATCH_AND_SCAN(tokenizer::token_type::COMMA);
			}
			UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
			instructions.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			length++;
		}
		SCAN;

		instructions.push_back({ .op = instance::opcode::ALLOCATE_FIXED, .operand = length });
		for (uint_fast32_t i = length; i >= 1; i--) {
			instructions.push_back({ .op = instance::opcode::DUPLICATE });
			instructions.push_back({ .op = instance::opcode::POP_SCRATCHPAD });

			instance::value val = instance.make_number(i);
			instructions.push_back({ .op = instance::opcode::STORE_TABLE_PROP, .operand = val.compute_hash() });
			instructions.push_back({ .op = instance::opcode::DISCARD_TOP });
		}
		break;
	}
	case tokenizer::token_type::OPEN_BRACE: {
		SCAN;
		uint32_t length = 0;
		while (tokenizer.match_last(tokenizer::token_type::CLOSE_BRACE))
		{
			if (length > 0) {
				MATCH_AND_SCAN(tokenizer::token_type::COMMA);
			}
			MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACE);
			UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
			instructions.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			MATCH_AND_SCAN(tokenizer::token_type::COMMA);
			UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
			instructions.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACE);
			length++;
		}
		SCAN;

		instructions.push_back({ .op = instance::opcode::REVERSE_SCRATCHPAD });
		instructions.push_back({ .op = instance::opcode::ALLOCATE_FIXED, .operand = length });
		for (uint_fast32_t i = 0; i < length; i++) {
			instructions.push_back({ .op = instance::opcode::DUPLICATE });
			instructions.push_back({ .op = instance::opcode::POP_SCRATCHPAD }); //pop key
			instructions.push_back({ .op = instance::opcode::POP_SCRATCHPAD }); //pop value
			instructions.push_back({ .op = instance::opcode::STORE_TABLE_ELEM });
			instructions.push_back({ .op = instance::opcode::DISCARD_TOP });
		}
		break;
	}
	case tokenizer::token_type::OPEN_PAREN:
		SCAN;
		UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
		MATCH_AND_SCAN(tokenizer::token_type::CLOSE_PAREN);
		break;
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
			std::string identifier = tokenizer.last_token().str();
			uint32_t hash = str_hash(identifier.c_str());
			SCAN;

			if (tokenizer.match_last(tokenizer::token_type::SET)) {
				SCAN;
				UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
				instructions.push_back({ .op = instance::opcode::STORE_TABLE_PROP, .operand = hash });
				return;
			}
			else {
				instructions.push_back({ .op = instance::opcode::LOAD_TABLE_PROP, .operand = hash });
				is_statement = false;
			}
			break;
		}
		case tokenizer::token_type::OPEN_BRACKET: {
			SCAN;
			UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
			MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACKET);

			if (tokenizer.match_last(tokenizer::token_type::SET)) {
				SCAN;
				UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
				instructions.push_back({ .op = instance::opcode::STORE_TABLE_ELEM });
				return;
			}
			else {
				instructions.push_back({ .op = instance::opcode::LOAD_TABLE_ELEM });
				is_statement = false;
			}
			break;
		}
		case tokenizer::token_type::OPEN_PAREN: {
			SCAN;

			instructions.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			uint32_t length = 0;
			while (!tokenizer.match_last(tokenizer::token_type::CLOSE_PAREN))
			{
				if (length > 0) {
					MATCH_AND_SCAN(tokenizer::token_type::COMMA);
				}
				UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
			}
			SCAN;

			instructions.push_back({ .op = instance::opcode::POP_SCRATCHPAD });
			instructions.push_back({ .op = instance::opcode::CALL, .operand = length });
			is_statement = true;
			break;
		}
		case tokenizer::token_type::QUESTION:
		{
			SCAN;
			uint32_t cond_ins_ip = instructions.size();
			instructions.push_back({ .op = instance::opcode::COND_JUMP_AHEAD });

			UNWRAP(compile_expression(tokenizer, instance, instructions, 0)); //if true value
			uint32_t jump_to_end_ip = instructions.size();
			instructions.push_back({ .op = instance::opcode::JUMP_AHEAD });

			MATCH_AND_SCAN(tokenizer::token_type::COLON);
			instructions[cond_ins_ip].operand = instructions.size() - cond_ins_ip;
			UNWRAP(compile_expression(tokenizer, instance, instructions, 0)); //if false value
			instructions[jump_to_end_ip].operand = instructions.size() - jump_to_end_ip;
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

std::optional<compiler::error> compiler::compile_expression(tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& instructions, int min_prec) {
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

	UNWRAP(compile_value(tokenizer, instance, instructions, false)); //lhs

	while (tokenizer.last_token().type >= tokenizer::token_type::PLUS && tokenizer.last_token().type <= tokenizer::token_type::AND && min_precs[tokenizer.last_token().type - tokenizer::token_type::PLUS] > min_prec)
	{
		SCAN;
		UNWRAP(compile_expression(tokenizer, instance, instructions, min_precs[tokenizer.last_token().type - tokenizer::token_type::PLUS]));
		instructions.push_back({ .op = (instance::opcode)((tokenizer.last_token().type - tokenizer::token_type::PLUS) + instance::opcode::ADD) });
	}

	return std::nullopt;
}

std::optional<compiler::error> compiler::compile_statement(tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& instructions) {
	tokenizer::token& token = tokenizer.last_token();
	source_loc& begin_loc = tokenizer.last_token_loc();

	switch (token.type)
	{
	case tokenizer::token_type::WHILE: {
		SCAN;
		uint32_t loop_begin_ip = instructions.size();
		UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
		uint32_t cond_jump_ip = instructions.size();
		instructions.push_back({ .op = instance::opcode::COND_JUMP_AHEAD });
		UNWRAP(compile_block(tokenizer, instance, instructions, [](tokenizer::token_type t) -> bool { return t == tokenizer::token_type::END_BLOCK; }));
		SCAN;
		instructions.push_back({ .op = instance::opcode::JUMP_BACK, .operand = (uint32_t)(instructions.size() - loop_begin_ip)});
		instructions[cond_jump_ip].operand = instructions.size() - cond_jump_ip;
		break;
	}
	case tokenizer::token_type::DO: {
		SCAN;
		uint32_t loop_begin_ip = instructions.size();
		UNWRAP(compile_block(tokenizer, instance, instructions, [](tokenizer::token_type t) -> bool { return t == tokenizer::token_type::WHILE; }));
		SCAN;
		UNWRAP(compile_expression(tokenizer, instance, instructions, 0));
		instructions.push_back({ .op = instance::opcode::JUMP_BACK, .operand = (uint32_t)(instructions.size() - loop_begin_ip) });
		break;
	}
	case tokenizer::token_type::IF: {
		SCAN;
		uint32_t last_cond_check_ip = 0;
		std::vector<uint32_t> jump_end_ips;
		do {
			if (jump_end_ips.size() > 0) {
				SCAN;
				instructions[last_cond_check_ip].operand = (instructions.size() - last_cond_check_ip);
			}

			UNWRAP(compile_expression(tokenizer, instance, instructions, 0)); //compile condition
			last_cond_check_ip = instructions.size();
			instructions.push_back({ .op = instance::opcode::COND_JUMP_AHEAD });

			UNWRAP(compile_block(tokenizer, instance, instructions, [](tokenizer::token_type t) -> bool { return t == tokenizer::token_type::END_BLOCK || t == tokenizer::token_type::ELIF || t == tokenizer::token_type::ELSE; }));
			jump_end_ips.push_back(instructions.size());
			instructions.push_back({ .op = instance::opcode::JUMP_AHEAD });
		} while (tokenizer.match_last(tokenizer::token_type::ELIF));

		if (tokenizer.match_last(tokenizer::token_type::ELSE)) {
			SCAN;
			instructions[last_cond_check_ip].operand = (instructions.size() - last_cond_check_ip);
			UNWRAP(compile_block(tokenizer, instance, instructions, [](tokenizer::token_type t) -> bool { return t == tokenizer::token_type::END_BLOCK; }));
		}
		else {
			SCAN;
			jump_end_ips.pop_back();
		}

		for (uint32_t ip : jump_end_ips) {
			instructions[ip].operand = instructions.size() - ip;
		}
		break;
	}
	case tokenizer::token_type::GLOBAL: {
		SCAN;
		MATCH(tokenizer::token_type::IDENTIFIER);
		std::string id = token.str();
		SCAN;
		MATCH_AND_SCAN(tokenizer::token_type::SET);

		UNWRAP(compile_expression(tokenizer, instance, instructions, 0));

		variable_symbol sym = {
			.name = id,
			.is_global = true,
			.local_id = max_globals
		};
		active_variables.insert({ id, sym });
		max_globals++;

		instructions.push_back({ .op = instance::opcode::STORE_GLOBAL, .operand = sym.local_id });
		return std::nullopt;
	}
	default:
		UNWRAP(compile_value(tokenizer, instance, instructions, true));
		instructions.push_back({ .op = instance::opcode::DISCARD_TOP });
		return std::nullopt;
	}
}

std::optional<compiler::error> compiler::compile_block(tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& instructions, bool(*stop_cond)(tokenizer::token_type)) {
	std::optional<compiler::error> toret = std::nullopt;

	scope_stack.push_back({ }); //push empty lexical scope
	while (!stop_cond(tokenizer.last_token().type))
	{
		toret = compile_statement(tokenizer, instance, instructions);
		if (toret.has_value())
			goto unwind_and_return;
	}
	
unwind_and_return:
	instructions.push_back({ .op = instance::opcode::UNWIND_LOCALS, .operand = (uint32_t)scope_stack.back().symbol_names.size() });
	for (std::string symbol : scope_stack.back().symbol_names) {
		active_variables.erase(symbol);
	}
	scope_stack.pop_back();
	return toret;
}