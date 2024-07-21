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
	scan_token();
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
#undef NEXT_HEX
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
			return last_tok = token(token_type::FUNCTION);
		case str_hash("array"):
			return last_tok = token(token_type::TABLE);
		case str_hash("class"):
			return last_tok = token(token_type::CLASS);
		case str_hash("if"):
			return last_tok = token(token_type::IF);
		case str_hash("else"):
			return last_tok = token(token_type::ELSE);
		case str_hash("elif"):
			return last_tok = token(token_type::ELIF);
		case str_hash("while"):
			return last_tok = token(token_type::WHILE);
		case str_hash("for"):
			return last_tok = token(token_type::FOR);
		case str_hash("do"):
			return last_tok = token(token_type::DO);
		case str_hash("return"):
			return last_tok = token(token_type::RETURN);
		case str_hash("break"):
			return last_tok = token(token_type::LOOP_BREAK);
		case str_hash("continue"):
			return last_tok = token(token_type::LOOP_CONTINUE);
		case str_hash("then"):
			return last_tok = token(token_type::THEN);
		case str_hash("end"):
			return last_tok = token(token_type::END_BLOCK);
		case str_hash("global"):
			return last_tok = token(token_type::GLOBAL);
		default:
			return last_tok = token(identifier);
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
			return last_tok = token(number);
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

		return last_tok = token(token_type::STRING_LITERAL, ss.str());
	}
	else {
		char switch_char = last_char;
		scan_char();
		switch (switch_char) {
		case '(':
			return last_tok = token(token_type::OPEN_PAREN);
		case ')':
			return last_tok = token(token_type::CLOSE_PAREN);
		case '{':
			return last_tok = token(token_type::OPEN_BRACE);
		case '}':
			return last_tok = token(token_type::CLOSE_BRACE);
		case '[':
			return last_tok = token(token_type::OPEN_BRACKET);
		case ']':
			return last_tok = token(token_type::CLOSE_BRACKET);
		case '.':
			return last_tok = token(token_type::PERIOD);
		case ',':
			return last_tok = token(token_type::COMMA);
		case '+':
			return last_tok = token(token_type::PLUS);
		case '-':
			return last_tok = token(token_type::MINUS);
		case '*':
			return last_tok = token(token_type::ASTERISK);
		case '/':
			return last_tok = token(token_type::SLASH);
		case '%':
			return last_tok = token(token_type::PERCENT);
		case '^':
			return last_tok = token(token_type::CARET);
		case '=':
			if (last_char == '=') {
				scan_char();
				return last_tok = token(token_type::EQUALS);
			}
			return last_tok = token(token_type::SET);
		case '>':
			if (last_char == '=') {
				scan_char();
				return last_tok = token(token_type::MORE_EQUAL);
			}
			return last_tok = token(token_type::MORE);
		case '<':
			if (last_char == '=') {
				scan_char();
				return last_tok = token(token_type::LESS_EQUAL);
			}
			return last_tok = token(token_type::LESS);
		case '!':
			if (last_char == '=') {
				scan_char();
				return last_tok = token(token_type::NOT_EQUAL);
			}
			return last_tok = token(token_type::NOT);
		case '&':
			if (last_char != '&')
				return error(error::etype::UNEXPECTED_CHAR, "Expected two ampersands(&&), but got something else.", token_begin);
			scan_char();
			return last_tok = token(token_type::AND);
		case '|':
			if (last_char != '|')
				return error(error::etype::UNEXPECTED_CHAR, "Expected two bars(||), but got something else.", token_begin);
			scan_char();
			return last_tok = token(token_type::OR);
		case '?':
			return last_tok = token(token_type::QUESTION);
		case ':':
			return last_tok = token(token_type::COLON);
		case '\0':
			return last_tok = token(token_type::END_OF_SOURCE);
		default:
		{
			std::stringstream ss;
			ss << "Unexpected character " << last_char << " while parsing token.";
			return error(error::etype::UNEXPECTED_CHAR, ss.str(), token_begin);
		}
		}
	}
}

compiler::error compiler::tokenizer::make_unexpected_tok_err(std::optional< compiler::tokenizer::token_type> expected) {
	static const char* tok_names[] = {
		"IDENTIFIER",
		"NUMBER",
		"STRING_LITERAL",

		"FUNCTION",
		"TABLE",
		"DICT",
		"CLASS",

		"IF",
		"ELIF",
		"ELSE",
		"WHILE",
		"FOR",
		"DO",
		"RETURN",

		"THEN",
		"END_BLOCK",

		"OPEN_PAREN",
		"CLOSE_PAREN",
		"OPEN_BRACE",
		"CLOSE_BRACE",
		"OPEN_BRACKET",
		"CLOSE_BRACKET",
		"PERIOD",
		"COMMA",

		"PLUS",
		"MINUS",
		"ASTERISK",
		"SLASH",
		"PERCENT",
		"CARET",

		"LESS",
		"MORE",
		"LESS_EQUAL",
		"MORE_EQUAL",
		"EQUALS",
		"NOT_EQUAL",
		"NOT",
		"SET",

		"AND",
		"OR",

		"END_OF_SOURCE"
	};

	std::stringstream ss;
	ss << "Got token " << tok_names[last_tok.type];
	if (last_tok.type == token_type::IDENTIFIER)
		ss << "(" << last_tok.str() << ")";
	
	if(expected.has_value())
		ss << ", but expected " << tok_names[expected.value()];

	ss << '.';

	return error(last_tok.type == token_type::END_OF_SOURCE ? error::etype::UNEXPECTED_EOF : error::etype::UNEXPECTED_TOKEN, ss.str(), token_begin);
}

std::optional<compiler::error> compiler::tokenizer::match(tokenizer::token_type expected) {
	if (last_tok.type == expected) {
		return std::nullopt;
	}

	return make_unexpected_tok_err(expected);
}