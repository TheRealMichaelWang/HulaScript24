#include <sstream>
#include <cmath>
#include <cassert>
#include "hash.h"
#include "compiler.h"

using namespace HulaScript::Compilation;

compiler::compiler(instance& target_instance, bool report_src_locs) : max_globals(0), max_instruction(0), repl_stop_parsing(false), target_instance(target_instance), report_src_locs(report_src_locs) {
	scope_stack.push_back({ });
	func_decl_stack.push_back({ .name = "top level local context", .max_locals = 0});
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

std::optional<error> compiler::compile_value(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool expects_statement, bool repl_mode) {
	token token = tokenizer.last_token();
	source_loc loc = tokenizer.last_token_loc();

	ip_src_map.insert({ (uint32_t)current_section.size(), loc });
	switch (token.type)
	{
	case token_type::IDENTIFIER: {
		std::string id = tokenizer.last_token().str();
		SCAN;

		auto local_it = active_variables.find(id);
		if (local_it != active_variables.end()) {
			if (tokenizer.match_last(token_type::SET)) {
				SCAN;
				UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));

				if (!local_it->second.is_global && local_it->second.func_id < func_decl_stack.size() - 1) {
					std::stringstream ss;
					ss << "Cannot set captured variable " << id << ", which was declared in " << func_decl_stack[local_it->second.func_id].name << ", not the current " << func_decl_stack.back().name << '.';
					return error(etype::CANNOT_SET_CAPTURED, ss.str(), loc);
				}
				
				current_section.push_back({ .op = local_it->second.is_global ? opcode::STORE_GLOBAL : opcode::STORE_LOCAL, .operand = local_it->second.local_id });
				if (expects_statement) {
					current_section.push_back({ .op = opcode::DISCARD_TOP });
				}
				return std::nullopt;
			}
			else {
				if (!local_it->second.is_global && local_it->second.func_id < func_decl_stack.size() - 1) {
					function_declaration& current_func = func_decl_stack.back();

					for (uint32_t i = (uint32_t)(func_decl_stack.size() - 1); i > local_it->second.func_id; i--) {
						auto capture_it = func_decl_stack[i].captured_vars.find(id);
						if (capture_it == func_decl_stack[i].captured_vars.end()) {
							func_decl_stack[i].captured_vars.insert(id);
						}
					}

					current_section.push_back({ .op = opcode::LOAD_LOCAL, .operand = 0 }); //load capture table, which is always local variable 0 in functions
					current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.make_string(id) });
					current_section.push_back({ .op = opcode::LOAD_TABLE_ELEM });
				}
				else {
					current_section.push_back({ .op = local_it->second.is_global ? opcode::LOAD_GLOBAL : opcode::LOAD_LOCAL, .operand = local_it->second.local_id });
				}
				break;
			}
		}
		else {
			auto class_it = class_decls.find(id);
			if (class_it != class_decls.end()) {
				return std::nullopt;
			}
			else if (tokenizer.match_last(token_type::SET) && (expects_statement || repl_mode)) { //declare variable here
				SCAN;
				UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));

				scope_stack.back().symbol_names.push_back(id);
				variable_symbol sym = {
					.name = id,
					.is_global = false,
					.local_id = func_decl_stack.back().max_locals,
					.func_id = (uint32_t)(func_decl_stack.size() - 1)
				};
				active_variables.insert({ id, sym });
				func_decl_stack.back().max_locals++;

				if (func_decl_stack.size() == 1) {
					declared_toplevel_locals.push_back(id);
				}
					
				current_section.push_back({ .op = opcode::DECL_LOCAL, .operand = sym.local_id });
				if (!expects_statement)
					current_section.push_back({ .op = opcode::LOAD_LOCAL, .operand = sym.local_id });
				return std::nullopt;
			}
			else {
				std::stringstream ss;
				ss << "Symbol " << id << " does not exist.";
				return error(etype::SYMBOL_NOT_FOUND, ss.str(), loc);
			}
		}
	}
	case token_type::MINUS:
		SCAN;
		if (tokenizer.match_last(token_type::NUMBER)) {
			current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant(Runtime::value(-token.number())) });
			SCAN;
		}
		else {
			UNWRAP(compile_value(tokenizer, current_section, ip_src_map, false, false));
			current_section.push_back({ .op = opcode::NEGATE });
		}
		break;
	case token_type::NOT:
		SCAN;
		UNWRAP(compile_value(tokenizer, current_section, ip_src_map, false, false));
		current_section.push_back({ .op = opcode::NOT });
		break;
	case token_type::NUMBER:
		current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant(Runtime::value(token.number())) });
		SCAN;
		break;
	case token_type::STRING_LITERAL:
		current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.make_string(token.str()) });
		SCAN;
		break;
	case token_type::NIL:
		SCAN;
		current_section.push_back({ .op = opcode::PUSH_NIL });
		break;
	case token_type::TRUE:
		current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant(Runtime::value(1.0)) });
		SCAN;
		break;
	case token_type::FALSE:
		current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant(Runtime::value(0.0)) });
		SCAN;
		break;
	case token_type::TABLE:
		SCAN;
		MATCH_AND_SCAN(token_type::OPEN_BRACKET);
		if (tokenizer.match_last(token_type::NUMBER)) {
			uint32_t length = (uint32_t)floor(tokenizer.last_token().number());
			current_section.push_back({ .op = opcode::ALLOCATE_FIXED, .operand = length });
			SCAN;
		}
		else {
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			ip_src_map.insert({ (uint32_t)current_section.size(), loc });
			current_section.push_back({ .op = opcode::ALLOCATE_DYN });
		}
		MATCH_AND_SCAN(token_type::OPEN_BRACKET);
		break;
	case token_type::OPEN_BRACKET: {
		SCAN;
		uint32_t length = 0;
		while (!tokenizer.match_last(token_type::CLOSE_BRACKET) && !tokenizer.match_last(token_type::END_OF_SOURCE))
		{
			if (length > 0) {
				MATCH_AND_SCAN(token_type::COMMA);
			}
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			current_section.push_back({ .op = opcode::PUSH_SCRATCHPAD });
			length++;
		}
		loc = tokenizer.last_token_loc();
		MATCH_AND_SCAN(token_type::CLOSE_BRACKET);

		ip_src_map.insert({ (uint32_t)current_section.size(), loc });
		current_section.push_back({ .op = opcode::ALLOCATE_FIXED, .operand = length });
		for (uint_fast32_t i = length; i >= 1; i--) {
			current_section.push_back({ .op = opcode::DUPLICATE });

			HulaScript::Runtime::value val((double)i);
			current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant(val) });
			current_section.push_back({ .op = opcode::POP_SCRATCHPAD });
			current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
			current_section.push_back({ .op = opcode::DISCARD_TOP });
		}
		break;
	}
	case token_type::OPEN_BRACE: {
		SCAN;
		uint32_t length = 0;
		while (!tokenizer.match_last(token_type::CLOSE_BRACE) && !tokenizer.match_last(token_type::END_OF_SOURCE))
		{
			if (length > 0) {
				MATCH_AND_SCAN(token_type::COMMA);
			}
			MATCH_AND_SCAN(token_type::OPEN_BRACE);
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			current_section.push_back({ .op = opcode::PUSH_SCRATCHPAD });
			MATCH_AND_SCAN(token_type::COMMA);
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			current_section.push_back({ .op = opcode::PUSH_SCRATCHPAD });
			MATCH_AND_SCAN(token_type::CLOSE_BRACE);
			length++;
		}
		loc = tokenizer.last_token_loc();
		MATCH_AND_SCAN(token_type::CLOSE_BRACE);

		ip_src_map.insert({ (uint32_t)current_section.size(), loc });
		current_section.push_back({ .op = opcode::REVERSE_SCRATCHPAD });
		current_section.push_back({ .op = opcode::ALLOCATE_FIXED, .operand = length });
		for (uint_fast32_t i = 0; i < length; i++) {
			current_section.push_back({ .op = opcode::DUPLICATE });
			current_section.push_back({ .op = opcode::POP_SCRATCHPAD }); //pop key
			current_section.push_back({ .op = opcode::POP_SCRATCHPAD }); //pop value
			current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
			current_section.push_back({ .op = opcode::DISCARD_TOP });
		}
		break;
	}
	case token_type::OPEN_PAREN:
		SCAN;
		UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
		MATCH_AND_SCAN(token_type::CLOSE_PAREN);
		break;
	case token_type::FUNCTION: {
		SCAN;
		std::stringstream ss;
		ss << "Anonymous function literal in " << func_decl_stack.back().name;
		UNWRAP(compile_function(ss.str(), tokenizer, current_section));
		break;
	}
	default:
		return tokenizer.make_unexpected_tok_err(std::nullopt);
	}

	bool is_statement = false;
	for (;;) {
		token = tokenizer.last_token();
		loc = tokenizer.last_token_loc();

		switch (token.type)
		{
		case token_type::PERIOD:
		{
			SCAN;
			MATCH(token_type::IDENTIFIER);
			current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.make_string(tokenizer.last_token().str()) });
			SCAN;

			if (tokenizer.match_last(token_type::SET)) {
				loc = tokenizer.last_token_loc();
				SCAN;
				UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
				ip_src_map.insert({ (uint32_t)current_section.size(), loc });
				current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
				current_section.push_back({ .op = opcode::DISCARD_TOP });
				return std::nullopt;
			}
			else {
				current_section.push_back({ .op = opcode::LOAD_TABLE_ELEM });
				is_statement = false;
			}
			break;
		}
		case token_type::OPEN_BRACKET: {
			SCAN;
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			MATCH_AND_SCAN(token_type::CLOSE_BRACKET);

			if (tokenizer.match_last(token_type::SET)) {
				loc = tokenizer.last_token_loc();
				SCAN;
				UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
				ip_src_map.insert({ (uint32_t)current_section.size(), loc });
				current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
				current_section.push_back({ .op = opcode::DISCARD_TOP });
				return std::nullopt;
			}
			else {
				ip_src_map.insert({ (uint32_t)current_section.size(), loc });
				current_section.push_back({ .op = opcode::LOAD_TABLE_ELEM });
				is_statement = false;
			}
			break;
		}
		case token_type::OPEN_PAREN: {
			SCAN;

			current_section.push_back({ .op = opcode::PUSH_SCRATCHPAD });
			uint32_t length = 0;
			while (!tokenizer.match_last(token_type::CLOSE_PAREN) && !tokenizer.match_last(token_type::END_OF_SOURCE))
			{
				if (length > 0) {
					MATCH_AND_SCAN(token_type::COMMA);
				}
				UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
				length++;
			}
			MATCH_AND_SCAN(token_type::CLOSE_PAREN);

			ip_src_map.insert({ (uint32_t)current_section.size(), loc });
			current_section.push_back({ .op = opcode::POP_SCRATCHPAD });
			current_section.push_back({ .op = opcode::CALL, .operand = length });
			is_statement = true;
			break;
		}
		case token_type::QUESTION:
		{
			SCAN;
			uint32_t cond_ins_ip = (uint32_t)current_section.size();
			current_section.push_back({ .op = opcode::COND_JUMP_AHEAD });

			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false)); //if true value
			uint32_t jump_to_end_ip = (uint32_t)current_section.size();
			current_section.push_back({ .op = opcode::JUMP_AHEAD });

			MATCH_AND_SCAN(token_type::COLON);
			current_section[cond_ins_ip].operand = (uint32_t)(current_section.size() - cond_ins_ip);
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false)); //if false value
			current_section[jump_to_end_ip].operand = (uint32_t)(current_section.size() - jump_to_end_ip);
			is_statement = false;

			goto return_immediatley;
		}
		default:
		return_immediatley:
			if (expects_statement) {
				if (is_statement) {
					current_section.push_back({ .op = opcode::DISCARD_TOP });
				}
				else {
					return error(etype::UNEXPECTED_VALUE, "Expected statement, but got value instead.", loc);
				}
			}
			return std::nullopt;
		}
	}
}

std::optional<error> compiler::compile_expression(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, int min_prec, bool repl_mode) {
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

	UNWRAP(compile_value(tokenizer, current_section, ip_src_map, false, repl_mode)); //lhs

	while (tokenizer.last_token().type >= token_type::PLUS && tokenizer.last_token().type <= token_type::AND && min_precs[tokenizer.last_token().type - token_type::PLUS] > min_prec)
	{
		token_type op_type = tokenizer.last_token().type;
		SCAN;
		UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, min_precs[tokenizer.last_token().type - token_type::PLUS], false));
		current_section.push_back({ .op = (opcode)((op_type - token_type::PLUS) + opcode::ADD) });
	}

	return std::nullopt;
}

std::optional<error> compiler::compile_statement(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool repl_mode) {
	token& token = tokenizer.last_token();
	source_loc& begin_loc = tokenizer.last_token_loc();

	ip_src_map.insert({ (uint32_t)current_section.size(), begin_loc });
	switch (token.type)
	{
	case token_type::WHILE: {
		SCAN;
		loop_stack.push_back({});
		uint32_t loop_begin_ip = (uint32_t)current_section.size();
		UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
		MATCH_AND_SCAN(token_type::DO);
		uint32_t cond_jump_ip = (uint32_t)current_section.size();
		current_section.push_back({ .op = opcode::COND_JUMP_AHEAD });
		UNWRAP(compile_block(tokenizer, current_section, ip_src_map, [](token_type t) -> bool { return t == token_type::END_BLOCK; }));
		MATCH_AND_SCAN(token_type::END_BLOCK);
		current_section.push_back({ .op = opcode::JUMP_BACK, .operand = (uint32_t)(current_section.size() - loop_begin_ip)});
		current_section[cond_jump_ip].operand = (uint32_t)(current_section.size() - cond_jump_ip);
		unwind_loop(loop_begin_ip, (uint32_t)current_section.size(), current_section);

		return std::nullopt;
	}
	case token_type::DO: {
		SCAN;
		loop_stack.push_back({});
		uint32_t loop_begin_ip = (uint32_t)current_section.size();
		UNWRAP_AND_HANDLE(compile_block(tokenizer, current_section, ip_src_map, [](token_type t) -> bool { return t == token_type::WHILE; }), loop_stack.pop_back());
		MATCH_AND_SCAN_AND_HANDLE(token_type::WHILE, loop_stack.pop_back());
		uint32_t continue_ip = (uint32_t)current_section.size();
		UNWRAP_AND_HANDLE(compile_expression(tokenizer, current_section, ip_src_map, 0, false), loop_stack.pop_back());
		current_section.push_back({ .op = opcode::JUMP_BACK, .operand = (uint32_t)(current_section.size() - loop_begin_ip) });
		unwind_loop(continue_ip, (uint32_t)current_section.size(), current_section);
		return std::nullopt;
	}
	case token_type::IF: {
		SCAN;
		uint32_t last_cond_check_ip = 0;
		std::vector<uint32_t> jump_end_ips;
		do {
			if (jump_end_ips.size() > 0) {
				SCAN;
				current_section[last_cond_check_ip].operand = (uint32_t)(current_section.size() - last_cond_check_ip);
			}

			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false)); //compile condition
			MATCH_AND_SCAN(token_type::THEN);
			last_cond_check_ip = (uint32_t)current_section.size();
			current_section.push_back({ .op = opcode::COND_JUMP_AHEAD });

			UNWRAP(compile_block(tokenizer, current_section, ip_src_map, [](token_type t) -> bool { return t == token_type::END_BLOCK || t == token_type::ELIF || t == token_type::ELSE; }));
			jump_end_ips.push_back((uint32_t)current_section.size());
			current_section.push_back({ .op = opcode::JUMP_AHEAD });
		} while (tokenizer.match_last(token_type::ELIF));

		current_section[last_cond_check_ip].operand = (uint32_t)(current_section.size() - last_cond_check_ip);
		if (tokenizer.match_last(token_type::ELSE)) {
			SCAN;
			UNWRAP(compile_block(tokenizer, current_section, ip_src_map, [](token_type t) -> bool { return t == token_type::END_BLOCK; }));
		}
		else {
			SCAN;
			jump_end_ips.pop_back();
		}

		for (uint32_t ip : jump_end_ips) {
			current_section[ip].operand = (uint32_t)(current_section.size() - ip);
		}
		return std::nullopt; 
	}
	case token_type::RETURN: {
		if (func_decl_stack.size() == 0) {
			return error(etype::UNEXPECTED_STATEMENT, "Unexpected return statement outside of function", begin_loc);
		}

		SCAN;
		UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
		current_section.push_back({ .op = opcode::RETURN });
		return std::nullopt;
	}
	case token_type::LOOP_BREAK:
	{
		if (loop_stack.size() == 0) {
			return error(etype::UNEXPECTED_STATEMENT, "Unexpected break statement outside of loop.", begin_loc);
		}

		loop_stack.back().break_requests.push_back((uint32_t)current_section.size());
		current_section.push_back({ .op = opcode::INVALID });
		return std::nullopt;
	}
	case token_type::LOOP_CONTINUE: {
		if (loop_stack.size() == 0) {
			return error(etype::UNEXPECTED_STATEMENT, "Unexpected continue statement outside of loop.", begin_loc);
		}

		loop_stack.back().continue_requests.push_back((uint32_t)current_section.size());
		current_section.push_back({ .op = opcode::INVALID });
		return std::nullopt;
	}
	default:
		if (repl_mode) {
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, true));
			MATCH(token_type::END_OF_SOURCE);
			repl_stop_parsing = true;
		}
		else {
			UNWRAP(compile_value(tokenizer, current_section, ip_src_map, true, false));
		}
		return std::nullopt;
	}
};

std::optional<error> compiler::compile_function(std::string name, tokenizer& tokenizer, std::vector<instruction>& current_section) {
	std::vector<std::string> param_ids;
	MATCH_AND_SCAN(token_type::OPEN_PAREN); 
	while (!tokenizer.match_last(token_type::CLOSE_PAREN) && !tokenizer.match_last(token_type::END_OF_SOURCE))
	{
		if (param_ids.size() > 0) {
			MATCH_AND_SCAN(token_type::COMMA);
		}
		MATCH(token_type::IDENTIFIER);
		std::string id = tokenizer.last_token().str();
		param_ids.push_back(id);
		UNWRAP(validate_symbol_availability(id, "function parameter", tokenizer.last_token_loc()));
		SCAN;
	}
	tokenizer.current_function_name = name;
	MATCH_AND_SCAN(token_type::CLOSE_PAREN);

	func_decl_stack.push_back({
		.name = name,
		.max_locals = (uint32_t)(1 + param_ids.size()),
	});
	scope_stack.push_back({ .symbol_names = param_ids });

	std::vector<instruction> func_instructions;
	std::map<uint32_t, source_loc> function_src_locs;
	uint32_t func_id = target_instance.emit_function_start(func_instructions);

	function_src_locs.insert({ 0, tokenizer.last_token_loc() });
	func_instructions.push_back({ .op = opcode::DECL_LOCAL, .operand = 0 });
	uint32_t param_id = 1;
	for (int_fast32_t i = (int_fast32_t)(param_ids.size() - 1); i >= 0; i--) {
		
		variable_symbol sym = {
			.name = param_ids[i],
			.is_global = false,
			.local_id = param_id,
			.func_id = (uint32_t)(func_decl_stack.size() - 1)
		};
		active_variables.insert({ param_ids[i], sym });
		func_instructions.push_back({ .op = opcode::DECL_LOCAL, .operand = sym.local_id });
	}

	while (!tokenizer.match_last(token_type::END_BLOCK) && !tokenizer.match_last(token_type::END_OF_SOURCE))
	{
		UNWRAP_AND_HANDLE(compile_statement(tokenizer, func_instructions, function_src_locs, false), {
			unwind_locals(func_instructions, false);
			func_decl_stack.pop_back();
			target_instance.available_function_ids.push_back(func_id);
			tokenizer.current_function_name = std::nullopt;
		});
	}
	tokenizer.current_function_name = std::nullopt;
	MATCH_AND_SCAN_AND_HANDLE(token_type::END_BLOCK, {
		unwind_locals(func_instructions, false);
		func_decl_stack.pop_back();
		target_instance.available_function_ids.push_back(func_id);
	});
	unwind_locals(func_instructions, false);
	{
		func_instructions.push_back({ .op = opcode::FUNCTION_END, .operand = (uint32_t)param_ids.size() });
		uint32_t old_size = (uint32_t)target_instance.loaded_instructions.size();
		target_instance.loaded_instructions.insert(target_instance.loaded_instructions.end(), func_instructions.begin(), func_instructions.end());

		if (report_src_locs) {
			for (std::pair<uint32_t, source_loc> loc : function_src_locs) {
				target_instance.ip_src_locs.insert({ old_size + loc.first, loc.second });
			}
		}

		uint32_t capture_size = (uint32_t)func_decl_stack.back().captured_vars.size();
		current_section.push_back({ .op = opcode::ALLOCATE_FIXED, .operand = capture_size });

		for (auto& captured_var : func_decl_stack.back().captured_vars) {
			current_section.push_back({ .op = opcode::DUPLICATE });
			
			auto var_it = active_variables.find(captured_var);
			uint32_t prop_str_id = target_instance.make_string(captured_var);
			current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = prop_str_id });

			if (var_it->second.func_id < func_decl_stack.size() - 2) { //this is a captured 
				current_section.push_back({ .op = opcode::LOAD_LOCAL, .operand = 0 }); //load capture table
				current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = prop_str_id});
				current_section.push_back({ .op = opcode::LOAD_TABLE_ELEM });
			}
			else { //load local 
				current_section.push_back({ .op = opcode::LOAD_LOCAL, .operand = var_it->second.local_id });
			}

			current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
			current_section.push_back({ .op = opcode::DISCARD_TOP });
		}

		current_section.push_back({ .op = opcode::MAKE_CLOSURE, .operand = func_id });
	}
	func_decl_stack.pop_back();
	return std::nullopt;
}

std::optional<error> compiler::compile(tokenizer& tokenizer, bool repl_mode) {
	std::vector<instruction> repl_section;
	max_instruction = target_instance.loaded_instructions.size();
	
	std::map<uint32_t, source_loc> ip_src_map;
	while (!repl_stop_parsing && !tokenizer.match_last(token_type::END_OF_SOURCE))
	{
		token& token = tokenizer.last_token();
		source_loc& begin_loc = tokenizer.last_token_loc();
		ip_src_map.insert({ (uint32_t)repl_section.size(), begin_loc });

		switch (token.type)
		{
		case token_type::GLOBAL: {
			SCAN_AND_HANDLE(unwind_error());
			MATCH_AND_HANDLE(token_type::IDENTIFIER, unwind_error());
			std::string id = token.str();
			UNWRAP_AND_HANDLE(validate_symbol_availability(id, "global variable", begin_loc), unwind_error());
			SCAN_AND_HANDLE(unwind_error());
			MATCH_AND_SCAN_AND_HANDLE(token_type::SET, unwind_error());

			UNWRAP_AND_HANDLE(compile_expression(tokenizer, repl_section, ip_src_map, 0, false), unwind_error());

			variable_symbol sym = {
				.name = id,
				.is_global = true,
				.local_id = max_globals
			};
			active_variables.insert({ id, sym });
			max_globals++;

			repl_section.push_back({ .op = opcode::DECL_GLOBAL, .operand = sym.local_id });

			if (repl_mode) {
				repl_section.push_back({ .op = opcode::LOAD_GLOBAL, .operand = sym.local_id });
				repl_stop_parsing = true;
			}
			break;
		}
		case token_type::FUNCTION: {
			SCAN_AND_HANDLE(unwind_error());
			MATCH_AND_HANDLE(token_type::IDENTIFIER, unwind_error());
			std::string id = token.str();
			UNWRAP_AND_HANDLE(validate_symbol_availability(id, "top-level function", begin_loc), unwind_error());
			SCAN_AND_HANDLE(unwind_error());

			variable_symbol sym = {
				.name = id,
				.is_global = true,
				.local_id = max_globals
			};
			active_variables.insert({ id, sym });
			max_globals++;
			declared_globals.push_back(id);

			UNWRAP_AND_HANDLE(compile_function(id, tokenizer, repl_section), unwind_error());
			repl_section.push_back({ .op = opcode::DECL_GLOBAL, .operand = sym.local_id });
			break;
		}
		default:
			UNWRAP_AND_HANDLE(compile_statement(tokenizer, repl_section, ip_src_map, repl_mode), unwind_error());
			break;
		}
	}

	if (repl_stop_parsing) {
		repl_stop_parsing = false;
	}
	uint32_t old_size = (uint32_t)target_instance.loaded_instructions.size();
	target_instance.loaded_instructions.insert(target_instance.loaded_instructions.end(), repl_section.begin(), repl_section.end());
	declared_globals.clear();
	declared_toplevel_locals.clear();
	if (report_src_locs) {
		for (std::pair<uint32_t, source_loc> loc : ip_src_map) {
			target_instance.ip_src_locs.insert({ old_size + loc.first, loc.second });
		}
	}
	return std::nullopt;
}

std::optional<error> compiler::compile_block(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool(*stop_cond)(token_type)) {
	std::optional<error> to_return = std::nullopt;

	scope_stack.push_back({ }); //push empty lexical scope
	while (!stop_cond(tokenizer.last_token().type) && !tokenizer.match_last(token_type::END_OF_SOURCE))
	{
		to_return = compile_statement(tokenizer, current_section, ip_src_map, false);
		if (to_return.has_value())
			goto unwind_and_return;
	}
	
unwind_and_return:
	unwind_locals(current_section, true);
	return to_return;
}

void compiler::unwind_locals(std::vector<instruction>& instructions, bool use_unwind_ins) {
	if (use_unwind_ins && scope_stack.back().symbol_names.size() > 0) {
		instructions.push_back({ .op = opcode::UNWIND_LOCALS, .operand = (uint32_t)scope_stack.back().symbol_names.size() });
	}
	for (std::string symbol : scope_stack.back().symbol_names) {
		active_variables.erase(symbol);
	}
	scope_stack.pop_back();
}

void compiler::unwind_loop(uint32_t cond_check_ip, uint32_t finish_ip, std::vector<instruction>& instructions) {
	for (uint32_t ip : loop_stack.back().break_requests) {
		assert(ip > finish_ip);
		instructions[ip] = {
			.op = opcode::JUMP_AHEAD,
			.operand = finish_ip - ip
		};
	}

	for (uint32_t ip : loop_stack.back().continue_requests) {
		if (ip > finish_ip) {
			instructions[ip] = {
				.op = opcode::JUMP_AHEAD,
				.operand = finish_ip - ip
			};
		}
		else {
			assert(ip != finish_ip);
			instructions[ip] = {
				.op = opcode::JUMP_BACK,
				.operand = ip - finish_ip
			};
		}
	}
}

void compiler::unwind_error() {
	for (auto& local : declared_toplevel_locals) {
		active_variables.erase(local);
	}
	func_decl_stack.back().max_locals -= declared_toplevel_locals.size();
	declared_toplevel_locals.clear();
	for (auto& global : declared_globals) {
		active_variables.erase(global);
	}
	max_globals -= declared_globals.size();
	declared_globals.clear();

	target_instance.loaded_instructions.erase(target_instance.loaded_instructions.begin() + max_instruction, target_instance.loaded_instructions.end());
	
	for (auto it = target_instance.ip_src_locs.lower_bound(max_globals); it != target_instance.ip_src_locs.end();) {
		it = target_instance.ip_src_locs.erase(it);
	}
}

std::optional<error> compiler::validate_symbol_availability(std::string id, std::string symbol_type, source_loc loc) {
	if (class_decls.contains(id)) {
		std::stringstream ss;
		ss << "Cannot declare " << symbol_type << ": a class named " << id << " already exists.";
		return error(etype::SYMBOL_ALREADY_EXISTS, ss.str(), loc);
	}
	else if (active_variables.contains(id)) {
		std::stringstream ss;
		ss << "Cannot declare " << symbol_type << ": a variable named " << id << " already exists.";
		return error(etype::SYMBOL_ALREADY_EXISTS, ss.str(), loc);
	}
	return std::nullopt;
}