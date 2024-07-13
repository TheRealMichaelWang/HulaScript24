#pragma once

#include <string>
#include <optional>
#include <variant>

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
				CANNOT_PARSE_NUMBER
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
				ARRAY,
				DICT,
				CLASS,

				IF,
				ELIF,
				ELSE,
				END,
				WHILE,
				FOR,
				DO,
				RETURN,

				OPEN_PAREN,
				CLOSE_PAREN,
				OPEN_BRACE,
				CLOSE_BRACE,
				OPEN_BRACKET,
				CLOSE_BRACKET,
				PERIOD,

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

				AND,
				OR,

				END
			};

			class token {
			public:
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
				token_type type;
				std::variant<std::monostate, std::string, double> payload;
			};

			tokenizer(std::string source, std::optional<std::string> file_source);

			token last_token() {
				return last_tok;
			}

			token scan_token();
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
		};
	};
}