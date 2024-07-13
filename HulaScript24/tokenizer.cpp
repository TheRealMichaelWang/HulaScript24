#include <cassert>
#include <cctype>
#include <sstream>
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

compiler::tokenizer::tokenizer(std::string source, std::optional<std::string> file_source) : current_row(1), current_col(0), next_row(1), next_col(1), token_begin(file_source), source(source), file_source(file_source), last_tok(token_type::END) {
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

compiler::tokenizer::token compiler::tokenizer::scan_token() {
	while (isspace(last_char)) {
		scan_char();
	}

	token_begin = source_loc(current_row, current_col, file_source);

	if (isalpha(last_char)) { //parse identifier
		std::stringstream identifier;
		while (isalnum(last_char) || last_char == '_') {
			identifier << last_char;
			scan_char();
		}
		return token(identifier.str());
	}
	else if (isdigit(last_char)) {
		std::stringstream numerical_ss;
		while (isdigit(last_char) || last_char == '.') {
			numerical_ss << last_char;
			scan_char();
		}
		

	}
}