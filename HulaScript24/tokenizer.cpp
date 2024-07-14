#include <cassert>
#include <cctype>
#include <sstream>
#include "hash.h"
#include "compiler.h"

using namespace HulaScript;

compiler::tokenizer::token::token(tokenizer::token_type type) : type(type), payload(std::monostate{}) {
	
}

compiler::tokenizer::token::token(tokenizer::token_type type, std::string identifier) : type(type), payload(identifier) {
	assert(type == tokenizer::token_type::IDENTIFIER || type == tokenizer::token_type::STRING_LITERAL);
}

compiler::tokenizer::token::token(std::string identifier) : type(tokenizer::token_type::IDENTIFIER), payload(identifier) {

}

compiler::tokenizer::token::token(double number) : type(tokenizer::token_type::NUMBER), payload(number) {

}

compiler::source_loc::source_loc(size_t row, size_t col, std::optional<std::string> file) : row(row), column(col), file(file) {

}

compiler::source_loc::source_loc(std::optional<std::string> file) : row(1), column(1), file(file) {

}

compiler::tokenizer::tokenizer(std::string source, std::optional<std::string> file_source) : current_row(1), current_col(0), next_row(1), next_col(1), token_begin(file_source), source(source), file_source(file_source), last_tok(token_type::END_OF_SOURCE) {
	scan_char();
}

char compiler::tokenizer::scan_char() {
	if (pos == source.size())
		return '\0';

	char to_return = source.at(pos);
	pos++;

	current_col = next_col;
	current_row = next_row;
	if (to_return == '\n') {
		next_col = 1;
		next_row++;
	}
	else {
		next_col++;
	}

	return last_char = to_return;
}

std::variant<char, compiler::error> compiler::tokenizer::scan_control() {
#define NEXT_HEX(NAME) char CHAR_##NAME = last_char;\
							scan_char();\
							int NAME;\
							if(CHAR_##NAME >= '0' && CHAR_##NAME <= '9') {\
								NAME = (int)(CHAR_##NAME - '0');\
							} else if(CHAR_##NAME >= 'a' && CHAR_##NAME <= 'f') {\
								NAME = (int)(CHAR_##NAME - 'a');\
							} else if(CHAR_##NAME >= 'A' && CHAR_##NAME <= 'F') {\
								NAME = (int)(CHAR_##NAME - 'A');\
							} else {\
								return error(error::etype::INVALID_CONTROL_CHAR, "Expected hexadecimal digit.", token_begin); \
							}

	if (last_char == '\\') {
		scan_char();
		switch (last_char)
		{
		case 'r':
			scan_char();
			return '\r';
		case 'n':
			scan_char();
			return '\n';
		case 't':
			scan_char();
			return '\t';
		case '\"':
			scan_char();
			return '\"';
		case '\'':
			scan_char();
			return '\'';
		case '0':
			scan_char();
			return '\0';
		case 'x': {
			scan_char();
			NEXT_HEX(hex_a);
			NEXT_HEX(hex_b);
			int total = hex_a << 8 + hex_b;
			return (char)total;
		}
		default: {
			if (last_char == '\0')
				return error(error::etype::UNEXPECTED_EOF, "Unexpected end in control sequence.", token_begin);
			else {
				std::stringstream ss;
				ss << last_char << " is an invalid control character.";
				return error(error::etype::INVALID_CONTROL_CHAR, ss.str(), token_begin);
			}
		}
		}
	}
	else if (last_char == '\0') {
		return error(error::etype::UNEXPECTED_EOF, "Unexpected end in string literal.", token_begin);
	}
	else {
		char to_return = last_char;
		scan_char();
		return to_return;
	}
}

std::variant<compiler::tokenizer::token, compiler::error> compiler::tokenizer::scan_token() {
	while (isspace(last_char)) {
		scan_char();
	}

	token_begin = source_loc(current_row, current_col, file_source);

	if (isalpha(last_char)) { //parse identifier
		std::stringstream identifier_ss;
		while (isalnum(last_char) || last_char == '_') {
			identifier_ss << last_char;
			scan_char();
		}
		std::string identifier = identifier_ss.str();

		uint64_t hash = str_hash(identifier.c_str());

		switch (hash)
		{
		case str_hash("function"):
			return token(token_type::FUNCTION);
		case str_hash("array"):
			return token(token_type::ARRAY);
		case str_hash("dict"):
			return token(token_type::DICT);
		case str_hash("class"):
			return token(token_type::CLASS);
		case str_hash("if"):
			return token(token_type::IF);
		case str_hash("else"):
			return token(token_type::ELSE);
		case str_hash("elif"):
			return token(token_type::ELIF);
		case str_hash("while"):
			return token(token_type::WHILE);
		case str_hash("for"):
			return token(token_type::FOR);
		case str_hash("do"):
			return token(token_type::DO);
		case str_hash("return"):
			return token(token_type::RETURN);
		case str_hash("then"):
			return token(token_type::THEN);
		case str_hash("end"):
			return token(token_type::END);
		default:
			return token(identifier);
		}
	}
	else if (isdigit(last_char)) {
		std::stringstream numerical_ss;
		while (isdigit(last_char) || last_char == '.') {
			numerical_ss << last_char;
			scan_char();
		}
		
		double number;
		try {
			number = std::stod(numerical_ss.str());
		}
		catch(...) {
			return error(error::etype::CANNOT_PARSE_NUMBER, "Cannot parse provided numerical string.", token_begin);
		}
	}
	else if (last_char == '\"') {
		scan_char();
		std::stringstream ss;
		while (last_char != '\"') {
			auto res = scan_control();

			if (std::holds_alternative<error>(res))
				return std::get<error>(res);
			ss << std::get<char>(res);
		}
		scan_char();

		return token(token_type::STRING_LITERAL, ss.str());
	}
}