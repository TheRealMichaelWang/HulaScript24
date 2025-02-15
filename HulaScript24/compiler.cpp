#include <sstream>
#include <cmath>
#include <cassert>
#include "hash.h"
#include "compiler.h"

using namespace HulaScript::Compilation;

compiler::compiler(instance& target_instance, bool report_src_locs) : max_globals(0), max_instruction(0), repl_stop_parsing(false), target_instance(target_instance), report_src_locs(report_src_locs), active_variables(16) {
	scope_stack.push_back({ });
	func_decl_stack.push_back({ .name = "top level local context", .max_locals = 0, .captured_vars = spp::sparse_hash_set<uint64_t>(4)});
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

	ip_src_map.insert({ static_cast<uint32_t>(current_section.size()), loc });
	bool value_is_self = false;
	switch (token.type)
	{
	case token_type::IDENTIFIER: {
		std::string id = tokenizer.last_token().str();
		uint64_t id_hash = str_hash(id.c_str());
		SCAN;

		auto local_it = active_variables.find(id_hash);
		if (local_it != active_variables.end()) {
			if (tokenizer.match_last(token_type::SET)) {
				SCAN;

				if (!local_it->second.is_global && local_it->second.func_id < func_decl_stack.size() - 1) {
					std::stringstream ss;
					ss << "Cannot set captured variable " << id << ", which was declared in " << func_decl_stack[local_it->second.func_id].name << ", not the current " << func_decl_stack.back().name << '.';
					return error(etype::CANNOT_SET_CAPTURED, ss.str(), loc);
				}

				UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
				current_section.push_back({ .op = local_it->second.is_global ? opcode::STORE_GLOBAL : opcode::STORE_LOCAL, .operand = local_it->second.local_id });
				if (expects_statement) {
					current_section.push_back({ .op = opcode::DISCARD_TOP });
				}
				return std::nullopt;
			}
			else {
				if (!local_it->second.is_global && local_it->second.func_id < func_decl_stack.size() - 1) {
					function_declaration& current_func = func_decl_stack.back();

					if (current_func.class_decl.has_value()) {
						return error(etype::CANNOT_CAPTURE_VAR, "Cannot capture variable from within a class method.", loc);
					}

					for (uint32_t i = static_cast<uint32_t>(func_decl_stack.size() - 1); i > local_it->second.func_id; i--) {
						auto capture_it = func_decl_stack[i].captured_vars.find(id_hash);
						if (capture_it == func_decl_stack[i].captured_vars.end()) {
							func_decl_stack[i].captured_vars.insert(id_hash);
						}
					}

					current_section.push_back({ .op = opcode::LOAD_LOCAL, .operand = 0 }); //load capture table, which is always local variable 0 in functions
					current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant_strhash(id_hash)});
					current_section.push_back({ .op = opcode::LOAD_TABLE_ELEM });
				}
				else {
					current_section.push_back({ .op = local_it->second.is_global ? opcode::LOAD_GLOBAL : opcode::LOAD_LOCAL, .operand = local_it->second.local_id });
				}
				break;
			}
		}
		else {
			if (tokenizer.match_last(token_type::SET) && (expects_statement || repl_mode)) { //declare variable here
				SCAN;
				UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));

				scope_stack.back().symbol_names.push_back(id_hash);
				variable_symbol sym = {
					.name = id,
					.is_global = false,
					.local_id = func_decl_stack.back().max_locals,
					.func_id = static_cast<uint32_t>(func_decl_stack.size() - 1)
				};
				active_variables.insert({ id_hash, sym });
				func_decl_stack.back().max_locals++;

				if (func_decl_stack.size() == 1 && scope_stack.size() == 1) {
					declared_toplevel_locals.push_back(id_hash);
					current_section.push_back({ .op = opcode::DECL_TOPLVL_LOCAL, .operand = sym.local_id });
				}
				else {
					current_section.push_back({ .op = opcode::DECL_LOCAL, .operand = sym.local_id });
				}
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
	case token_type::SELF:
		if (!func_decl_stack.back().class_decl.has_value() || func_decl_stack.size() != 2) {
			return error(etype::UNEXPECTED_TOKEN, "Self keyword cannot be used outside of a class method.", loc);
		}
		SCAN;
		current_section.push_back({ .op = opcode::LOAD_LOCAL, .operand = 0 });
		value_is_self = true;
		break;
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
	case token_type::STRING_LITERAL: {
		current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant(Runtime::value((char*)token.str().c_str())) });
		SCAN;
		break;
	}
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
			uint32_t length = static_cast<uint32_t>(floor(tokenizer.last_token().number()));
			current_section.push_back({ .op = opcode::ALLOCATE_FIXED, .operand = length });
			SCAN;
		}
		else {
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			ip_src_map.insert({ static_cast<uint32_t>(current_section.size()), loc });
			current_section.push_back({ .op = opcode::ALLOCATE_DYN });
		}
		MATCH_AND_SCAN(token_type::CLOSE_BRACKET);
		break;
	case token_type::OPEN_BRACKET: {
		SCAN;
		uint32_t allocate_ins = static_cast<uint32_t>(current_section.size());
		current_section.push_back({ .op = opcode::ALLOCATE_FIXED });
		uint32_t length = 0;
		while (!tokenizer.match_last(token_type::CLOSE_BRACKET) && !tokenizer.match_last(token_type::END_OF_SOURCE))
		{
			if (length > 0) {
				MATCH_AND_SCAN(token_type::COMMA);
			}
			current_section.push_back({ .op = opcode::DUPLICATE });
			HulaScript::Runtime::value val((double)length);
			current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant_key(val) });
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
			current_section.push_back({ .op = opcode::DISCARD_TOP });
			length++;
		}
		MATCH_AND_SCAN(token_type::CLOSE_BRACKET);
		current_section[allocate_ins].operand = length;
		break;
	}
	case token_type::OPEN_BRACE: {
		SCAN;
		uint32_t allocate_ins = static_cast<uint32_t>(current_section.size());
		current_section.push_back({ .op = opcode::ALLOCATE_FIXED });
		uint32_t length = 0;
		while (!tokenizer.match_last(token_type::CLOSE_BRACE) && !tokenizer.match_last(token_type::END_OF_SOURCE))
		{
			if (length > 0) {
				MATCH_AND_SCAN(token_type::COMMA);
			}
			current_section.push_back({ .op = opcode::DUPLICATE });

			MATCH_AND_SCAN(token_type::OPEN_BRACE);
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			MATCH_AND_SCAN(token_type::COMMA);
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			MATCH_AND_SCAN(token_type::CLOSE_BRACE);

			current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
			current_section.push_back({ .op = opcode::DISCARD_TOP });

			length++;
		}
		MATCH_AND_SCAN(token_type::CLOSE_BRACE);
		current_section[allocate_ins].operand = length;
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
		UNWRAP(compile_function(ss.str(), tokenizer, current_section, std::nullopt, loc));
		break;
	}
	case token_type::IF: {
		SCAN;
		UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));

		MATCH_AND_SCAN(token_type::THEN);
		uint32_t cond_check_ip = static_cast<uint32_t>(current_section.size());
		current_section.push_back({ .op = opcode::COND_JUMP_AHEAD });
		UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
		uint32_t jump_final_ip = static_cast<uint32_t>(current_section.size());
		current_section.push_back({ .op = opcode::JUMP_AHEAD });

		MATCH_AND_SCAN(token_type::ELSE);
		current_section[cond_check_ip].operand = static_cast<uint32_t>(current_section.size() - cond_check_ip);
		UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
		current_section[jump_final_ip].operand = static_cast<uint32_t>(current_section.size() - jump_final_ip);
		
		if (expects_statement) {
			return error(etype::UNEXPECTED_VALUE, "Expected statement, but got if-else value instead.", loc);
		}
		
		return std::nullopt;
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
			uint64_t prop_hash = str_hash(tokenizer.last_token().str().c_str());
			if (value_is_self && !func_decl_stack.back().class_decl.value()->properties.contains(prop_hash)) {
				std::stringstream ss;
				ss << "Class " << func_decl_stack.back().class_decl.value()->name << " doesn't have property " << tokenizer.last_token().str() << '.';
				return error(etype::SYMBOL_NOT_FOUND, ss.str(), tokenizer.last_token_loc());
			}
			current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant_strhash(prop_hash) });
			SCAN;

			if (tokenizer.match_last(token_type::SET)) {
				loc = tokenizer.last_token_loc();
				SCAN;
				UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
				ip_src_map.insert({ static_cast<uint32_t>(current_section.size()), loc });
				current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
				current_section.push_back({ .op = opcode::DISCARD_TOP });
				return std::nullopt;
			}
			else {
				current_section.push_back({ .op = opcode::LOAD_TABLE_ELEM });
				is_statement = false;
				value_is_self = false;
				break;
			}
		}
		case token_type::OPEN_BRACKET: {
			SCAN;
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			MATCH_AND_SCAN(token_type::CLOSE_BRACKET);

			if (tokenizer.match_last(token_type::SET)) {
				loc = tokenizer.last_token_loc();
				SCAN;
				UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
				ip_src_map.insert({ static_cast<uint32_t>(current_section.size()), loc });
				current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
				current_section.push_back({ .op = opcode::DISCARD_TOP });
				return std::nullopt;
			}
			else {
				ip_src_map.insert({ static_cast<uint32_t>(current_section.size()), loc });
				current_section.push_back({ .op = opcode::LOAD_TABLE_ELEM });
				is_statement = false;
				value_is_self = false;
				break;
			}
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

			ip_src_map.insert({ static_cast<uint32_t>(current_section.size()), loc });
			current_section.push_back({ .op = opcode::POP_SCRATCHPAD });
			current_section.push_back({ .op = opcode::CALL, .operand = length });
			is_statement = true;
			value_is_self = false;
			break;
		}
		default:
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
		1, //or
		3, //nil coaleasing operator
	};

	UNWRAP(compile_value(tokenizer, current_section, ip_src_map, false, repl_mode)); //lhs

	while (tokenizer.last_token().type >= token_type::PLUS && tokenizer.last_token().type <= token_type::NIL_COALESING && min_precs[tokenizer.last_token().type - token_type::PLUS] > min_prec)
	{
		token_type op_type = tokenizer.last_token().type;
		SCAN;

		if (op_type == token_type::NIL_COALESING) {
			uint32_t jump_addr = static_cast<uint32_t>(current_section.size());
			current_section.push_back({ .op = opcode::IFNT_NIL_JUMP_AHEAD });
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, min_precs[op_type - token_type::PLUS], false));
			current_section[jump_addr].operand = static_cast<uint32_t>(current_section.size() - jump_addr);
		}
		else {
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, min_precs[op_type - token_type::PLUS], false));
			current_section.push_back({ .op = (opcode)((op_type - token_type::PLUS) + opcode::ADD) });
		}
	}

	return std::nullopt;
}

std::optional<error> compiler::compile_statement(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool repl_mode) {
	token& token = tokenizer.last_token();
	source_loc& begin_loc = tokenizer.last_token_loc();

	ip_src_map.insert({ static_cast<uint32_t>(current_section.size()), begin_loc });
	switch (token.type)
	{
	case token_type::WHILE: {
		SCAN;
		loop_stack.push_back({ .break_local_count = func_decl_stack.back().max_locals, .continue_local_count = func_decl_stack.back().max_locals });
		uint32_t loop_begin_ip = static_cast<uint32_t>(current_section.size());
		UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
		MATCH_AND_SCAN(token_type::DO);
		uint32_t cond_jump_ip = static_cast<uint32_t>(current_section.size());
		current_section.push_back({ .op = opcode::COND_JUMP_AHEAD });
		uint32_t probe_ip = static_cast<uint32_t>(current_section.size());
		UNWRAP(compile_block(tokenizer, current_section, ip_src_map, [](token_type t) -> bool { return t == token_type::END_BLOCK; }));
		MATCH_AND_SCAN(token_type::END_BLOCK);
		current_section.push_back({ .op = opcode::JUMP_BACK, .operand = static_cast<uint32_t>(current_section.size() - loop_begin_ip)});
		unwind_loop(loop_begin_ip, static_cast<uint32_t>(current_section.size()), current_section);
		current_section[cond_jump_ip].operand = static_cast<uint32_t>(current_section.size() - cond_jump_ip);

		return std::nullopt;
	}
	case token_type::DO: {
		SCAN;
		loop_stack.push_back({ .break_local_count = func_decl_stack.back().max_locals, .continue_local_count = func_decl_stack.back().max_locals });
		uint32_t loop_begin_ip = static_cast<uint32_t>(current_section.size());
		UNWRAP_AND_HANDLE(compile_block(tokenizer, current_section, ip_src_map, [](token_type t) -> bool { return t == token_type::WHILE; }), loop_stack.pop_back());
		MATCH_AND_SCAN_AND_HANDLE(token_type::WHILE, loop_stack.pop_back());
		uint32_t continue_ip = static_cast<uint32_t>(current_section.size());
		UNWRAP_AND_HANDLE(compile_expression(tokenizer, current_section, ip_src_map, 0, false), loop_stack.pop_back());
		current_section.push_back({ .op = opcode::JUMP_BACK, .operand = static_cast<uint32_t>(current_section.size() - loop_begin_ip) });
		unwind_loop(continue_ip, static_cast<uint32_t>(current_section.size()), current_section);
		return std::nullopt;
	}
	case token_type::IF: {
		SCAN;
		uint32_t last_cond_check_ip = 0;
		std::vector<uint32_t> jump_end_ips;
		do {
			if (jump_end_ips.size() > 0) {
				SCAN;
				current_section[last_cond_check_ip].operand = static_cast<uint32_t>(current_section.size() - last_cond_check_ip);
			}

			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false)); //compile condition
			MATCH_AND_SCAN(token_type::THEN);
			last_cond_check_ip = static_cast<uint32_t>(current_section.size());
			current_section.push_back({ .op = opcode::COND_JUMP_AHEAD });

			UNWRAP(compile_block(tokenizer, current_section, ip_src_map, [](token_type t) -> bool { return t == token_type::END_BLOCK || t == token_type::ELIF || t == token_type::ELSE; }));
			jump_end_ips.push_back(static_cast<uint32_t>(current_section.size()));
			current_section.push_back({ .op = opcode::JUMP_AHEAD });
		} while (tokenizer.match_last(token_type::ELIF));

		current_section[last_cond_check_ip].operand = static_cast<uint32_t>(current_section.size() - last_cond_check_ip);
		if (tokenizer.match_last(token_type::ELSE)) {
			SCAN;
			UNWRAP(compile_block(tokenizer, current_section, ip_src_map, [](token_type t) -> bool { return t == token_type::END_BLOCK; }));
		}
		else {
			SCAN;
			jump_end_ips.pop_back();
		}
		
		for (uint32_t ip : jump_end_ips) {
			current_section[ip].operand = static_cast<uint32_t>(current_section.size() - ip);
		}
		return std::nullopt; 
	}
	case token_type::FOR: {
		SCAN;
		MATCH(token_type::IDENTIFIER);
		std::string id = tokenizer.last_token().str();
		uint64_t id_hash = str_hash(id.c_str());
		validate_symbol_availability(id, " iterator variable ", tokenizer.last_token_loc());
		SCAN;
		MATCH_AND_SCAN(token_type::IN);
		UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
		MATCH_AND_SCAN(token_type::DO);

		scope_stack.push_back({ });
		uint32_t probe_ip = static_cast<uint32_t>(current_section.size());
		current_section.push_back({ .op = opcode::PROBE_LOCALS });

		scope_stack.back().symbol_names.push_back(id_hash);
		variable_symbol sym = {
			.name = id,
			.is_global = false,
			.local_id = func_decl_stack.back().max_locals,
			.func_id = static_cast<uint32_t>(func_decl_stack.size() - 1)
		};
		active_variables.insert({ id_hash, sym });
		func_decl_stack.back().max_locals++;
		current_section.push_back({ .op = opcode::PUSH_NIL });
		current_section.push_back({ .op = opcode::DECL_LOCAL, .operand = sym.local_id });

		loop_stack.push_back({ .break_local_count = func_decl_stack.back().max_locals - 1, .continue_local_count = func_decl_stack.back().max_locals - 1 });

		uint32_t loop_begin_ip = static_cast<uint32_t>(current_section.size());
		current_section.push_back({ .op = opcode::DUPLICATE });
		uint32_t check_jump_ip = static_cast<uint32_t>(current_section.size());
		current_section.push_back({ .op = opcode::IF_NIL_JUMP_AHEAD });
		emit_call_method("elem", current_section);
		current_section.push_back({ .op = opcode::STORE_LOCAL, .operand = sym.local_id });
		current_section.push_back({ .op = opcode::DISCARD_TOP });

		while (!tokenizer.match_last(token_type::END_BLOCK) && !tokenizer.match_last(token_type::END_OF_SOURCE)) {
			UNWRAP_AND_HANDLE(compile_statement(tokenizer, current_section, ip_src_map, false), unwind_locals(current_section, probe_ip, false));
		}
		emit_call_method("next", current_section);
		current_section.push_back({ .op = opcode::JUMP_BACK, .operand = static_cast<uint32_t>(current_section.size() - loop_begin_ip) });
		current_section[check_jump_ip].operand = static_cast<uint32_t>(current_section.size() - check_jump_ip);
		unwind_loop(loop_begin_ip, current_section.size(), current_section);
		current_section.push_back({ .op = opcode::DISCARD_TOP });
		unwind_locals(current_section, probe_ip, true);
		MATCH_AND_SCAN(token_type::END_BLOCK);

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
	case token_type::LOOP_BREAK: {
		SCAN;
		if (loop_stack.size() == 0) {
			return error(etype::UNEXPECTED_STATEMENT, "Unexpected break statement outside of loop.", begin_loc);
		}

		if (func_decl_stack.back().max_locals > loop_stack.back().break_local_count) {
			current_section.push_back({ .op = opcode::UNWIND_LOCALS, .operand = (func_decl_stack.back().max_locals - loop_stack.back().break_local_count) });
		}
		loop_stack.back().break_requests.push_back(static_cast<uint32_t>(current_section.size()));
		current_section.push_back({ .op = opcode::INVALID });
		return std::nullopt;
	}
	case token_type::LOOP_CONTINUE: {
		SCAN;
		if (loop_stack.size() == 0) {
			return error(etype::UNEXPECTED_STATEMENT, "Unexpected continue statement outside of loop.", begin_loc);
		}

		if (func_decl_stack.back().max_locals > loop_stack.back().continue_local_count) {
			current_section.push_back({ .op = opcode::UNWIND_LOCALS, .operand = (func_decl_stack.back().max_locals - loop_stack.back().continue_local_count) });
		}
		loop_stack.back().continue_requests.push_back(static_cast<uint32_t>(current_section.size()));
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

std::optional<error> compiler::compile_function(std::string name, tokenizer& tokenizer, std::vector<instruction>& current_section, std::optional<class_declaration*> class_decl, source_loc begin_loc) {
	if (name == "constructor" && class_decl.has_value() && class_decl.value()->constructor.has_value()) {
		std::stringstream ss;
		ss << "A constructor for class " << class_decl.value()->name << "; cannot redeclare constructor.";
		return error(etype::SYMBOL_ALREADY_EXISTS, ss.str(), begin_loc);
	}
	std::vector<std::string> param_ids;
	std::vector<uint64_t> param_hashes;
	MATCH_AND_SCAN(token_type::OPEN_PAREN); 
	while (!tokenizer.match_last(token_type::CLOSE_PAREN) && !tokenizer.match_last(token_type::END_OF_SOURCE))
	{
		if (param_ids.size() > 0) {
			MATCH_AND_SCAN(token_type::COMMA);
		}
		MATCH(token_type::IDENTIFIER);
		std::string id = tokenizer.last_token().str();
		param_ids.push_back(id);
		param_hashes.push_back(str_hash(id.c_str()));
		UNWRAP(validate_symbol_availability(id, "function parameter", tokenizer.last_token_loc()));
		SCAN;
	}
	tokenizer.current_function_name = name;
	MATCH_AND_SCAN(token_type::CLOSE_PAREN);

	func_decl_stack.push_back({
		.name = name,
		.max_locals = static_cast<uint32_t>(1 + param_ids.size()),
		.captured_vars = spp::sparse_hash_set<uint64_t>(4),
		.class_decl = class_decl
	});
	scope_stack.push_back({ .symbol_names = param_hashes });

	std::vector<instruction> func_instructions;
	std::map<uint32_t, source_loc> function_src_locs;
	uint32_t func_id = target_instance.emit_function_start(func_instructions);

	function_src_locs.insert({ 0, tokenizer.last_token_loc() });
	func_instructions.push_back({ .op = opcode::PROBE_LOCALS });
	func_instructions.push_back({ .op = opcode::DECL_LOCAL, .operand = 0 });
	uint32_t param_id = 1;
	for (int_fast32_t i = (int_fast32_t)(param_ids.size() - 1); i >= 0; i--) {
		
		variable_symbol sym = {
			.name = param_ids[i],
			.is_global = false,
			.local_id = param_id,
			.func_id = static_cast<uint32_t>(func_decl_stack.size() - 1)
		};
		active_variables.insert({ str_hash(param_ids[i].c_str()), sym });
		func_instructions.push_back({ .op = opcode::DECL_LOCAL, .operand = sym.local_id });
		param_id++;
	}

	while (!tokenizer.match_last(token_type::END_BLOCK) && !tokenizer.match_last(token_type::END_OF_SOURCE))
	{
		UNWRAP_AND_HANDLE(compile_statement(tokenizer, func_instructions, function_src_locs, false), {
			unwind_locals(func_instructions, 0, false);
			func_decl_stack.pop_back();
			target_instance.available_function_ids.push_back(func_id);
			tokenizer.current_function_name = std::nullopt;
		});
	}
	tokenizer.current_function_name = std::nullopt;
	MATCH_AND_SCAN_AND_HANDLE(token_type::END_BLOCK, {
		unwind_locals(func_instructions, 0, false);
		func_decl_stack.pop_back();
		target_instance.available_function_ids.push_back(func_id);
	});
	unwind_locals(func_instructions, 0, false);
	{
		uint32_t expected_params = static_cast<uint32_t>(param_ids.size());
		if (class_decl.has_value()) {
			if (name == "construct") {
				class_decl.value()->constructor = std::make_pair(func_id, static_cast<uint32_t>(param_ids.size()));
				func_instructions.push_back({ .op = opcode::LOAD_LOCAL, .operand = 0 });
				func_instructions.push_back({ .op = opcode::RETURN });
				expected_params++;
			}
			else {
				uint64_t name_hash = str_hash(name.c_str());
				class_decl.value()->methods.insert({ name_hash, func_id });
			}
		}

		func_instructions.push_back({ .op = opcode::FUNCTION_END, .operand = expected_params });
		func_instructions[1].operand = func_decl_stack.back().max_locals;
		uint32_t old_size = static_cast<uint32_t>(target_instance.loaded_instructions.size());
		target_instance.loaded_instructions.insert(target_instance.loaded_instructions.end(), func_instructions.begin(), func_instructions.end());
		
		if (report_src_locs) {
			for (std::pair<uint32_t, source_loc> loc : function_src_locs) {
				target_instance.ip_src_locs.insert({ old_size + loc.first, loc.second });
			}
		}

		if (!class_decl.has_value()) { //handle captured variables
			uint32_t capture_size = static_cast<uint32_t>(func_decl_stack.back().captured_vars.size());
			current_section.push_back({ .op = opcode::ALLOCATE_FIXED, .operand = capture_size });

			for (auto& captured_var : func_decl_stack.back().captured_vars) {
				current_section.push_back({ .op = opcode::DUPLICATE });

				auto var_it = active_variables.find(captured_var);
				uint32_t prop_str_id = target_instance.add_constant_strhash(captured_var);
				current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = prop_str_id });

				if (var_it->second.func_id < func_decl_stack.size() - 2) { //this is a captured 
					current_section.push_back({ .op = opcode::LOAD_LOCAL, .operand = 0 }); //load capture table
					current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = prop_str_id });
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
	}
	func_decl_stack.pop_back();
	return std::nullopt;
}

std::optional<error> compiler::compile_class(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map) {
	assert(func_decl_stack.size() == 1);
	MATCH_AND_SCAN(token_type::CLASS);
	MATCH(token_type::IDENTIFIER);

	class_declaration declaration = {
		.name = tokenizer.last_token().str(),
		.properties = spp::sparse_hash_set<uint64_t>(12),
		.methods = spp::sparse_hash_map<uint64_t, uint32_t>(4),
	};
	uint64_t name_hash = str_hash(declaration.name.c_str());
	variable_symbol sym = {
		.name = declaration.name,
		.is_global = true,
		.local_id = max_globals
	};
	active_variables.insert({name_hash, sym});
	max_globals++;
	declared_globals.push_back(name_hash);
	SCAN;

	spp::sparse_hash_set<uint64_t> default_value_properties;
	std::vector<uint64_t> ordered_properties;
	
	uint32_t alloc_table_ip = static_cast<uint32_t>(current_section.size());
	current_section.push_back({ .op = opcode::ALLOCATE_FIXED });
	while (tokenizer.match_last(token_type::IDENTIFIER))
	{
		uint64_t id = str_hash(tokenizer.last_token().str().c_str());

		if (declaration.properties.contains(id)) {
			std::stringstream ss;
			ss << "Class " << declaration.name << " already defines property " << tokenizer.last_token().str() << "; cannot redefine property.";
			return error(etype::SYMBOL_ALREADY_EXISTS, ss.str(), tokenizer.last_token_loc());
		}

		declaration.properties.insert(id);
		ordered_properties.push_back(id);
		SCAN;

		if (tokenizer.match_last(token_type::SET)) {
			SCAN;
			current_section.push_back({ .op = opcode::DUPLICATE });
			current_section.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant_strhash(id) });
			UNWRAP(compile_expression(tokenizer, current_section, ip_src_map, 0, false));
			current_section.push_back({ .op = opcode::STORE_TABLE_ELEM });
			current_section.push_back({ .op = opcode::DISCARD_TOP });
			default_value_properties.insert(id);
		}
	}
	current_section[alloc_table_ip].operand = static_cast<uint32_t>(default_value_properties.size());

	while (tokenizer.match_last(token_type::FUNCTION))
	{
		source_loc func_begin = tokenizer.last_token_loc();
		SCAN;
		MATCH(token_type::IDENTIFIER);
		std::string name = tokenizer.last_token().str();
		SCAN;

		UNWRAP(compile_function(name, tokenizer, current_section, &declaration, func_begin));
	}
	MATCH_AND_SCAN(token_type::END_BLOCK);

	std::vector<instruction> func_instructions;
	uint32_t func_id = target_instance.emit_function_start(func_instructions);
	uint32_t probe_ip = static_cast<uint32_t>(func_instructions.size());
	func_instructions.push_back({ .op = opcode::PROBE_LOCALS });
	func_instructions.push_back({ .op = opcode::DECL_LOCAL, .operand = 0 });
	
	func_instructions.push_back({ .op = opcode::ALLOCATE_FIXED, .operand = static_cast<uint32_t>(declaration.properties.size() + declaration.methods.size()) });
	func_instructions.push_back({ .op = opcode::PUSH_SCRATCHPAD });
	for (uint64_t id : default_value_properties) {
		uint32_t prop_hash_id = target_instance.add_constant_strhash(id);
		func_instructions.push_back({ .op = opcode::PEEK_SCRATCHPAD });
		func_instructions.push_back({ .op = opcode::LOAD_CONSTANT, .operand = prop_hash_id});
		func_instructions.push_back({ .op = opcode::LOAD_LOCAL, .operand = 0 });
		func_instructions.push_back({ .op = opcode::LOAD_CONSTANT, .operand = prop_hash_id });
		func_instructions.push_back({ .op = opcode::LOAD_TABLE_ELEM });
		func_instructions.push_back({ .op = opcode::STORE_TABLE_ELEM });
		func_instructions.push_back({ .op = opcode::DISCARD_TOP });
	}
	for (auto func : declaration.methods) {
		func_instructions.push_back({ .op = opcode::PEEK_SCRATCHPAD });
		func_instructions.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant_strhash(func.first) });
		func_instructions.push_back({ .op = opcode::PEEK_SCRATCHPAD });
		func_instructions.push_back({ .op = opcode::MAKE_CLOSURE, .operand = func.second });
		func_instructions.push_back({ .op = opcode::STORE_TABLE_ELEM });
		func_instructions.push_back({ .op = opcode::DISCARD_TOP });
	}

	func_instructions.push_back({ .op = opcode::POP_SCRATCHPAD });

	uint32_t param_length;
	if (declaration.constructor.has_value()) {
		auto constructor = declaration.constructor.value();
		//func_instructions.push_back({ .op = opcode::LOAD_LOCAL, .operand = 1 });
		func_instructions.push_back({ .op = opcode::CALL_NO_CAPUTRE_TABLE, .operand = constructor.first });
		//func_instructions.push_back({ .op = opcode::DISCARD_TOP });
		param_length = constructor.second;
	}
	else {
		func_instructions[probe_ip].operand = 2;
		func_instructions.push_back({ .op = opcode::DECL_LOCAL, .operand = 1 });
		for (auto it = ordered_properties.rbegin(); it != ordered_properties.rend(); it++) {
			if (!default_value_properties.contains(*it)) {
				func_instructions.push_back({ .op = opcode::PUSH_SCRATCHPAD });
				func_instructions.push_back({ .op = opcode::LOAD_LOCAL, .operand = 1 });
				func_instructions.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant_strhash(*it) });
				func_instructions.push_back({ .op = opcode::POP_SCRATCHPAD });
				func_instructions.push_back({ .op = opcode::STORE_TABLE_ELEM });
				func_instructions.push_back({ .op = opcode::DISCARD_TOP });
			}
		}
		param_length = static_cast<uint32_t>(ordered_properties.size() - default_value_properties.size());
		func_instructions.push_back({ .op = opcode::LOAD_LOCAL, .operand = 1 });
	}
	func_instructions.push_back({ .op = opcode::RETURN });
	func_instructions.push_back({ .op = opcode::FUNCTION_END, .operand = param_length });

	target_instance.loaded_instructions.insert(target_instance.loaded_instructions.end(), func_instructions.begin(), func_instructions.end());

	current_section.push_back({ .op = opcode::MAKE_CLOSURE, .operand = func_id });
	current_section.push_back({ .op = opcode::DECL_GLOBAL, .operand = sym.local_id });
	return std::nullopt;
}

std::optional<error> compiler::compile(tokenizer& tokenizer, bool repl_mode) {
	std::vector<instruction> repl_section;
	max_instruction = static_cast<uint32_t>(target_instance.loaded_instructions.size());
	func_decl_stack.back().max_locals = target_instance.top_level_local_offset;
	max_globals = target_instance.global_offset;
	
	std::map<uint32_t, source_loc> ip_src_map;
	while (!repl_stop_parsing && !tokenizer.match_last(token_type::END_OF_SOURCE))
	{
		token& token = tokenizer.last_token();
		source_loc& begin_loc = tokenizer.last_token_loc();
		ip_src_map.insert({ static_cast<uint32_t>(repl_section.size()), begin_loc });

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
			uint64_t hash = str_hash(id.c_str());
			active_variables.insert({ hash, sym});
			max_globals++;
			declared_globals.push_back(hash);

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
			uint64_t hash = str_hash(id.c_str());
			active_variables.insert({ hash, sym });
			max_globals++;
			declared_globals.push_back(hash);

			UNWRAP_AND_HANDLE(compile_function(id, tokenizer, repl_section, std::nullopt, begin_loc), unwind_error());
			repl_section.push_back({ .op = opcode::DECL_GLOBAL, .operand = sym.local_id });
			break;
		}
		case token_type::CLASS:
			UNWRAP_AND_HANDLE(compile_class(tokenizer, repl_section, ip_src_map), unwind_error());
			break;
		default:
			UNWRAP_AND_HANDLE(compile_statement(tokenizer, repl_section, ip_src_map, repl_mode), unwind_error());
			break;
		}
	}

	if (repl_stop_parsing) {
		repl_stop_parsing = false;
	}
	if (declared_toplevel_locals.size() > 0) {
		target_instance.loaded_instructions.push_back({ .op = opcode::PROBE_LOCALS, .operand = static_cast<uint32_t>(declared_toplevel_locals.size()) });
	}
	if (declared_globals.size() > 0) {
		target_instance.loaded_instructions.push_back({ .op = opcode::PROBE_GLOBALS, .operand = static_cast<uint32_t>(declared_globals.size()) });
	}
	uint32_t old_size = static_cast<uint32_t>(target_instance.loaded_instructions.size());
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
	uint32_t probe_ip = static_cast<uint32_t>(current_section.size());
	current_section.push_back({ .op = opcode::PROBE_LOCALS });
	while (!stop_cond(tokenizer.last_token().type) && !tokenizer.match_last(token_type::END_OF_SOURCE))
	{
		to_return = compile_statement(tokenizer, current_section, ip_src_map, false);
		if (to_return.has_value())
			goto unwind_and_return;
	}
	
unwind_and_return:
	unwind_locals(current_section, probe_ip, true);
	return to_return;
}

void compiler::unwind_locals(std::vector<instruction>& instructions, uint32_t probe_ip, bool use_unwind_ins) {
	if (use_unwind_ins) {
		if (scope_stack.back().symbol_names.size() > 0) {
			instructions[probe_ip].operand = static_cast<uint32_t>(scope_stack.back().symbol_names.size());
			instructions.push_back({ .op = opcode::UNWIND_LOCALS, .operand = static_cast<uint32_t>(scope_stack.back().symbol_names.size()) });
		}
		else {
			auto it = instructions.erase(instructions.begin() + probe_ip);
			for (; it != instructions.end(); it++) {
				if (it->op == opcode::JUMP_BACK) {
					uint32_t ip = static_cast<uint32_t>(it - instructions.begin());
					uint32_t target_ip = ip - it->operand;
					if (target_ip <= probe_ip) {
						it->operand--;
					}
				}
			}
		}
	}
	for (uint64_t symbol : scope_stack.back().symbol_names) {
		active_variables.erase(symbol);
	}
	func_decl_stack.back().max_locals -= scope_stack.back().symbol_names.size();
	scope_stack.pop_back();
}

void compiler::unwind_loop(uint32_t cond_check_ip, uint32_t finish_ip, std::vector<instruction>& instructions) {
	for (uint32_t ip : loop_stack.back().break_requests) {
		//assert(ip > finish_ip);
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
			//assert(ip != finish_ip);
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
	func_decl_stack.back().max_locals -= static_cast<uint32_t>(declared_toplevel_locals.size());
	declared_toplevel_locals.clear();
	for (auto& global : declared_globals) {
		active_variables.erase(global);
	}
	max_globals -= static_cast<uint32_t>(declared_globals.size());
	declared_globals.clear();

	target_instance.loaded_instructions.erase(target_instance.loaded_instructions.begin() + max_instruction, target_instance.loaded_instructions.end());
	
	for (auto it = target_instance.ip_src_locs.lower_bound(max_globals); it != target_instance.ip_src_locs.end();) {
		it = target_instance.ip_src_locs.erase(it);
	}
}

void compiler::emit_call_method(std::string method_name, std::vector<instruction>& instructions) {
	uint64_t method_id_hash = str_hash(method_name.c_str());
	instructions.push_back({ .op = opcode::LOAD_CONSTANT, .operand = target_instance.add_constant_strhash(method_id_hash) });
	instructions.push_back({ .op = opcode::LOAD_TABLE_ELEM });
	instructions.push_back({ .op = opcode::CALL, .operand = 0 });
}

std::optional<error> compiler::validate_symbol_availability(std::string id, std::string symbol_type, source_loc loc) {
	uint64_t hash = str_hash(id.c_str());
	if (active_variables.contains(hash)) {
		std::stringstream ss;
		ss << "Cannot declare " << symbol_type << ": a variable named " << id << " already exists.";
		return error(etype::SYMBOL_ALREADY_EXISTS, ss.str(), loc);
	}
	return std::nullopt;
}