#include <sstream>
#include <cmath>
#include "hash.h"
#include "compiler.h"

using namespace HulaScript;

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

std::optional<compiler::error> compiler::compile_value(compiler::tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& instructions) {
	tokenizer::token token = tokenizer.last_token();

	switch (token.type)
	{
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
			UNWRAP(compile_expression(tokenizer, instance, instructions));
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
			UNWRAP(compile_expression(tokenizer, instance, instructions));
			instructions.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			length++;
		}
		SCAN;

		instructions.push_back({ .op = instance::opcode::ALLOCATE_FIXED, .operand = length });
		for (uint_fast32_t i = length; i >= 1; i--) {
			instructions.push_back({ .op = instance::opcode::DUPLICATE });
			instructions.push_back({ .op = instance::opcode::POP_SCRATCHPAD });

			instance::value val = {
				.type = instance::value::vtype::NUMBER,
				.data = {
					.number = i
				}
			};

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
			UNWRAP(compile_expression(tokenizer, instance, instructions));
			instructions.push_back({ .op = instance::opcode::PUSH_SCRATCHPAD });
			MATCH_AND_SCAN(tokenizer::token_type::COMMA);
			UNWRAP(compile_expression(tokenizer, instance, instructions));
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
		UNWRAP(compile_expression(tokenizer, instance, instructions));
		MATCH_AND_SCAN(tokenizer::token_type::CLOSE_PAREN);
		break;
	default:
		return tokenizer.make_unexpected_tok_err(std::nullopt);
	}

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
				UNWRAP(compile_expression(tokenizer, instance, instructions));
				instructions.push_back({ .op = instance::opcode::STORE_TABLE_PROP, .operand = hash });
				return;
			}
			else {
				instructions.push_back({ .op = instance::opcode::LOAD_TABLE_PROP, .operand = hash });
			}
			break;
		}
		case tokenizer::token_type::OPEN_BRACKET: {
			SCAN;
			UNWRAP(compile_expression(tokenizer, instance, instructions));
			MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACKET);

			if (tokenizer.match_last(tokenizer::token_type::SET)) {
				SCAN;
				UNWRAP(compile_expression(tokenizer, instance, instructions));
				instructions.push_back({ .op = instance::opcode::STORE_TABLE_ELEM });
				return;
			}
			else {
				instructions.push_back({ .op = instance::opcode::LOAD_TABLE_ELEM });
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
				UNWRAP(compile_expression(tokenizer, instance, instructions))
			}
			SCAN;

			instructions.push_back({ .op = instance::opcode::POP_SCRATCHPAD });
			instructions.push_back({ .op = instance::opcode::CALL, .operand = length });
			break;
		}
		default:
			return;
		}
	}
}