#include <sstream>
#include <cmath>
#include "hash.h"
#include "compiler.h"

using namespace HulaScript;

static const char* type_names[] = {
	"array",
	"dict",
	"class",
	"func",
	"str",
	"num",
	"ANY/NO TYPE"
};

#define UNWRAP(RESNAME, RES) auto RESNAME = RES; if(std::holds_alternative<error>(RESNAME)) { return std::get<error>(RESNAME); }

#define SCAN {UNWRAP(scan_res, tokenizer.scan_token())};

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

std::variant<compiler::value_info, compiler::error> compiler::compile_value(compiler::tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& instructions) {
	tokenizer::token token = tokenizer.last_token();

	value_info current_info;
	source_loc value_loc = tokenizer.last_token_loc();
	switch (token.type)
	{
	case tokenizer::token_type::NUMBER:
		instructions.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_number(token.number())) });
		current_info = {
			.type = value_type::NUMBER
		};
		SCAN;
		break;
	case tokenizer::token_type::STRING_LITERAL:
		instructions.push_back({ .op = instance::opcode::LOAD_CONSTANT, .operand = instance.add_constant(instance.make_string(token.str())) });
		SCAN;
		current_info = {
			.type = value_type::STRING
		};
		break;
	case tokenizer::token_type::ARRAY:
		SCAN;
		MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACKET);
		if (tokenizer.match_last(tokenizer::token_type::NUMBER)) {
			uint32_t length = floor(tokenizer.last_token().number());
			instructions.push_back({ .op = instance::opcode::ALLOCATE_ARRAY_FIXED, .operand = length });
			current_info = {
				.type = value_type::ARRAY,
				.array_length = length
			};
			SCAN;
		}
		else {
			UNWRAP(len_res, compile_expression(tokenizer, instance, instructions));
			auto len_info = std::get<value_info>(len_res);

			instructions.push_back({ .op = instance::opcode::ALLOCATE_ARRAY });
			current_info = {
				.type = value_type::ARRAY,
				.array_length = len_info.array_length
			};
		}
		MATCH_AND_SCAN(tokenizer::token_type::OPEN_BRACKET);
		break;
	case tokenizer::token_type::DICT:
		SCAN;
		if (tokenizer.match_last(tokenizer::token_type::OPEN_BRACKET)) {
			SCAN;
			MATCH(tokenizer::token_type::NUMBER);
			uint32_t length = floor(tokenizer.last_token().number());
			SCAN;
			MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACKET);
			instructions.push_back({ .op = instance::opcode::ALLOCATE_DICT, .operand = length });
		}
		else {
			instructions.push_back({ .op = instance::opcode::ALLOCATE_DICT, .operand = 0 });
		}
		current_info = {
			.type = value_type::DICTIONARY
		};
		break;
	case tokenizer::token_type::OPEN_BRACKET: {
		SCAN;
		uint32_t length = 0;
		while (tokenizer.match_last(tokenizer::token_type::CLOSE_BRACKET))
		{
			if (length > 0) {
				MATCH_AND_SCAN(tokenizer::token_type::COMMA);
			}
			UNWRAP(elem_res, compile_expression(tokenizer, instance, instructions));

			length++;
		}
		instructions.push_back({ .op = instance::opcode::ALLOCATE_ARRAY_LITERAL, .operand = length });

		current_info = {
			.type = value_type::ARRAY,
			.array_length = length
		};
		break;
	}
	default:
		return tokenizer.make_unexpected_tok_err(std::nullopt);
	}

	for (;;) {
		token = tokenizer.last_token();
		value_loc = tokenizer.last_token_loc();
		switch (token.type)
		{
		case tokenizer::token_type::PERIOD:
		{
			SCAN;
			source_loc prop_loc = tokenizer.last_token_loc();
			MATCH(tokenizer::token_type::IDENTIFIER);
			std::string identifier = tokenizer.last_token().str();
			uint32_t hash = str_hash(identifier.c_str());
			SCAN;

			if (current_info.type != value_type::OBJECT && current_info.type != value_type::ANY) {
				std::stringstream ss;
				ss << "Cannot access property from value of type " << type_names[current_info.type] << '.';
				return error(error::etype::TYPE_ERROR, ss.str(), value_loc);
			}

			if (current_info.class_decl.has_value()) {
				if (!current_info.class_decl.value().properties.contains(hash)) {
					std::stringstream ss;
					ss << "Property " << identifier << " doesn't exist in class " << current_info.class_decl.value().name << '.';
					return error(error::etype::PROPERTY_DOES_NOT_EXIST, ss.str(), prop_loc);
				}
			}

			if (tokenizer.match_last(tokenizer::token_type::SET)) {
				SCAN;
				source_loc set_val_begin = tokenizer.last_token_loc();
				UNWRAP(set_res, compile_expression(tokenizer, instance, instructions));
				auto set_info = std::get<value_info>(set_res);
				if (current_info.class_decl.has_value()) {
					auto prop_it = current_info.class_decl.value().properties.find(hash);
					
					if (prop_it->second.type != value_type::ANY && prop_it->second.type != set_info.type) {
						std::stringstream ss;
						ss << "Property " << identifier << " expects type " << type_names[prop_it->second.type] << " and cannot be set to value of type " << type_names[set_info.type] << '.';
						return error(error::etype::TYPE_ERROR, ss.str(), set_val_begin);
					}
				}

				instructions.push_back({ .op = instance::opcode::STORE_OBJ_PROP, .operand = hash });
				return set_info;
			}
			else {
				instructions.push_back({ .op = instance::opcode::LOAD_OBJ_PROP, .operand = hash });
				if (current_info.class_decl.has_value()) {
					auto it = current_info.class_decl.value().properties.find(hash);
					current_info = {
						.type = it->second.type,
					};
				}
				else
					current_info = { .type = value_type::ANY };
			}
			break;
		}
		case tokenizer::token_type::OPEN_BRACKET: {
			SCAN;
			std::optional<uint32_t> fixed_index;
			if (tokenizer.match(tokenizer::token_type::NUMBER) && current_info.type == value_type::ARRAY) {
				double number = tokenizer.last_token().number();
				if (number < 0) {
					return error(error::etype::INDEX_OUT_OF_RANGE, "Array index cannot be less than zero.", tokenizer.last_token_loc());
				}
				uint32_t index = floor(tokenizer.last_token().number());
				if (current_info.array_length.has_value() && index >= current_info.array_length.value()) {
					std::stringstream ss;
					ss << "Index " << index << " is greater than/equal to array length of " << current_info.array_length.value() << ".";
					return error(error::etype::INDEX_OUT_OF_RANGE, ss.str(), tokenizer.last_token_loc());
				}
				fixed_index = index;
				SCAN;
			}
			else {
				source_loc begin_index_loc = tokenizer.last_token_loc();
				UNWRAP(index_res, compile_expression(tokenizer, instance, instructions));
				fixed_index = std::nullopt;
				auto index_info = std::get<value_info>(index_res);

				if (current_info.type == value_type::ARRAY && index_info.type != value_type::NUMBER && index_info.type != value_type::ANY) {
					std::stringstream ss;
					ss << "Array index must be a number, not a " << type_names[index_info.type] << '.';
					return error(error::etype::TYPE_ERROR, ss.str(), begin_index_loc);
				}
			}
			MATCH_AND_SCAN(tokenizer::token_type::CLOSE_BRACKET);

			if (tokenizer.match_last(tokenizer::token_type::SET)) {
				SCAN;
				UNWRAP(set_res, compile_expression(tokenizer, instance, instructions));
				auto set_info = std::get<value_info>(set_res);

				if (current_info.type == value_type::ARRAY) {
					if (fixed_index.has_value()) {
						instructions.push_back({ .op = instance::opcode::STORE_ARRAY_FIXED, .operand = fixed_index.value() });
					}
					else {
						instructions.push_back({ .op = instance::opcode::STORE_ARRAY_ELEM });
					}
				}
				else {
					instructions.push_back({ .op = instance::opcode::STORE_DICT_ELEM });
				}
				return set_info;
			}
			else {
				if (current_info.type == value_type::ARRAY) {
					if (fixed_index.has_value()) {
						instructions.push_back({ .op = instance::opcode::LOAD_ARRAY_FIXED, .operand = fixed_index.value() });
					}
					else {
						instructions.push_back({ .op = instance::opcode::LOAD_ARRAY_ELEM });
					}
				}
				else {
					instructions.push_back({ .op = instance::opcode::LOAD_DICT_ELEM });
				}
				current_info = { .type = value_type::ANY };
				break;
			}
		}
		case tokenizer::token_type::OPEN_PAREN: {
			SCAN;

			if (current_info.func_decl.has_value()) {
				bool first = true;
				for (function_declaration::param param : current_info.func_decl.value().param_types) {
					if (first) { first = false; }
					else {
						MATCH_AND_SCAN(tokenizer::token_type::COMMA);
					}

					UNWRAP(arg_res, compile_expression(tokenizer, instance, instructions));
					auto arg_info = std::get<value_info>(arg_res);

					if (param.type == value_type::ANY) {

					}
				}
			}

			break;
		}
		default:
			return current_info;
		}
	}
}