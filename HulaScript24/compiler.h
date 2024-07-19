#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <variant>
#include <vector>
#include <set>
#include "instance.h"

namespace HulaScript {
	class compiler {
	public:
		class source_loc {
		public:
			source_loc(size_t row, size_t col, std::optional<std::string> file);
			source_loc(std::optional<std::string> file);
		private:
			size_t row;
			size_t column;
			std::optional<std::string> file;
		};

		class error {
		public:
			enum etype {
				CANNOT_PARSE_NUMBER,
				INVALID_CONTROL_CHAR,
				UNEXPECTED_CHAR,
				UNEXPECTED_EOF,
				UNEXPECTED_TOKEN,
				PROPERTY_DOES_NOT_EXIST,
				INDEX_OUT_OF_RANGE,
				TYPE_ERROR,
			} type;

			error(etype type, source_loc location);
			error(etype type, std::string message, source_loc location);

		private:
			std::optional<std::string> msg;
			source_loc location;
		};

		class tokenizer {
		public:
			enum token_type {
				IDENTIFIER,
				NUMBER,
				STRING_LITERAL,

				FUNCTION,
				TABLE,
				DICT,
				CLASS,

				IF,
				ELIF,
				ELSE,
				WHILE,
				FOR,
				DO,
				RETURN,

				THEN,
				END,

				OPEN_PAREN,
				CLOSE_PAREN,
				OPEN_BRACE,
				CLOSE_BRACE,
				OPEN_BRACKET,
				CLOSE_BRACKET,
				PERIOD,
				COMMA,

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
				NOT,
				SET,

				AND,
				OR,

				END_OF_SOURCE
			};

			class token {
			public:
				token_type type;

				token(token_type type);
				token(token_type type, std::string identifier);
				token(std::string identifier);
				token(double number);

				std::string str() {
					return std::get<std::string>(payload);
				}

				double number() {
					return std::get<double>(payload);
				}
			private:
				std::variant<std::monostate, std::string, double> payload;
			};

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
			std::optional<error> match(tokenizer::token_type expected);

			compiler::error make_unexpected_tok_err(std::optional< compiler::tokenizer::token_type> expected);
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
			std::variant<char, compiler::error> scan_control();
		};

		compiler();

	private:
		struct class_declaration {
			std::string name;
			std::vector<std::string> properties;
		};

		struct function_declaration {
			std::string name;

			std::vector<std::string> params;
		};

		std::optional<error> compile_value(tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& instructions);
		std::optional<error> compile_expression(tokenizer& tokenizer, instance& instance, std::vector<instance::instruction>& instructions);
	};
}