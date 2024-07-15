#include <sstream>
#include <cmath>
#include "compiler.h"

using namespace HulaScript;

#define MATCH(EXPECTED_TOK) { auto res = tokenizer.match(EXPECTED_TOK);\
								if(res.has_value()) {\
									return res;\
								}}

#define MATCH_AND_SCAN(EXPECTED_TOK) { auto res = tokenizer.match(EXPECTED_TOK);\
						if(res.has_value()) {\
							return res;\
						}\
						tokenizer.scan_token();}

#define IF_AND_SCAN(TOK) if(tokenizer.last_tok().type == TOK) { tokenizer.scan_token(); }

std::optional<compiler::error> compiler::compile_value(compiler::tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& instructions) {
	for (;;) {
		tokenizer::token token = tokenizer.last_token();

		switch (token.type)
		{
		case tokenizer::token_type::NUMBER:
			instructions.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_number(token.number())) });
			tokenizer.scan_token();
			break;
		case tokenizer::token_type::STRING_LITERAL:
			instructions.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_string(token.str())) });
			tokenizer.scan_token();
			break;
		case tokenizer::token_type::ARRAY:
			tokenizer.scan_token();
			MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACKET);
			if (tokenizer.match_last(tokenizer::token_type::NUMBER)) {
				uint32_t length = floor(tokenizer.last_token().number());
				instructions.push_back({ .op = instance::opcode::ALLOCATE_ARRAY_FIXED, .operand = length });
				tokenizer.scan_token();
			}
			else {
				compile_value(tokenizer, instance, instructions);
				instructions.push_back({ .op = instance::opcode::ALLOCATE_ARRAY });
			}
			MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACKET);
			break;
		case tokenizer::token_type::DICT:
			tokenizer.scan_token();
			if (tokenizer.conditional_scan(tokenizer::token_type::OPEN_BRACKET)) {
				MATCH(tokenizer::token_type::NUMBER);
				uint32_t length = floor(tokenizer.last_token().number());
				tokenizer.scan_token();
				MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACKET);
				instructions.push_back({ .op = instance::opcode::ALLOCATE_DICT, .operand = length });
			}
			else {
				instructions.push_back({ .op = instance::opcode::ALLOCATE_DICT, .operand = 0 });
			}
			break;
		}
	}
}