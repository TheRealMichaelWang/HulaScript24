#pragma once

#include <string>
#include <optional>
#include <variant>
#include "error.h"

namespace HulaScript::Compilation {
	enum token_type {
		IDENTIFIER,
		NUMBER,
		STRING_LITERAL,

		TRUE,
		FALSE,
		NIL,

		FUNCTION,
		TABLE,
		CLASS,
		SELF,

		IF,
		ELIF,
		ELSE,
		WHILE,
		FOR,
		DO,
		RETURN,
		LOOP_BREAK,
		LOOP_CONTINUE,
		GLOBAL,

		THEN,
		END_BLOCK,

		OPEN_PAREN,
		CLOSE_PAREN,
		OPEN_BRACE,
		CLOSE_BRACE,
		OPEN_BRACKET,
		CLOSE_BRACKET,
		PERIOD,
		COMMA,
		QUESTION,
		COLON,

		PLUS,
		MINUS,
		ASTERISK,
		SLASH,
		PERCENT,
		CARET,

		LESS,
		MORE,
		LESS_EQUAL,
		MORE_EQUAL,
		EQUALS,
		NOT_EQUAL,

		AND,
		OR,

		NOT,
		SET,
		END_OF_SOURCE
	};

	class token {
	public:
		token_type type;

		token(token_type type) : type(type), payload(std::monostate{}) { }
		token(token_type type, std::string identifier) : type(type), payload(identifier) { }
		token(std::string identifier) : type(token_type::IDENTIFIER), payload(identifier) { }
		token(double number) : type(token_type::NUMBER), payload(number) { }

		std::string str() {
			return std::get<std::string>(payload);
		}

		double number() {
			return std::get<double>(payload);
		}
	private:
		std::variant<std::monostate, std::string, double> payload;
	};

	class tokenizer {
	public:
		tokenizer(std::string source, std::optional<std::string> file_source);

		token& last_token() {
			return last_tok;
		}

		source_loc& last_token_loc() {
			return token_begin;
		}

		bool match_last(token_type type) {
			return last_tok.type == type;
		}

		std::variant<token, error> scan_token();
		std::optional<error> match(token_type expected);

		error make_unexpected_tok_err(std::optional<token_type> expected);

		std::optional<std::string> current_function_name;
	private:
		size_t current_row;
		size_t current_col;
		size_t next_row;
		size_t next_col;
		size_t pos;
		source_loc token_begin;
		token last_tok;
		char last_char;

		std::string source;
		std::optional<std::string> file_source;

		char scan_char();
		std::variant<char, error> scan_control();
	};
}