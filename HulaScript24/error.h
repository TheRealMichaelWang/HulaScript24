#pragma once

#include <cstdint>
#include <string>
#include <optional>

namespace HulaScript {
	class source_loc {
	public:
		source_loc(size_t row, size_t col, std::optional<std::string> file, std::optional<std::string> function_name) : row(row), column(col), file(file), function_name(function_name) { }
		source_loc(std::optional<std::string> file, std::optional<std::string> function_name) : row(1), column(1), file(file), function_name(function_name) { }
	
		std::string to_print_string();

	private:
		size_t row;
		size_t column;
		std::optional<std::string> file;
		std::optional<std::string> function_name;
	};
}

namespace HulaScript::Runtime {
	enum etype {
		UNEXPECTED_TYPE,
		ARGUMENT_COUNT_MISMATCH,
		MEMORY,
		INTERNAL_ERROR
	};

	class error {
	public:
		error(error::etype type, std::string message, std::optional<source_loc> source_loc, uint32_t ip) : type(type), msg(message), location(source_loc), ip(ip) { }
		error(etype type, std::optional<source_loc> source_loc, uint32_t ip) : type(type), msg(std::nullopt), location(source_loc), ip(ip) { }

		std::string to_print_string();

	private:
		etype type;
		std::optional<std::string> msg;
		std::optional<source_loc> location;
		uint32_t ip;
	};
}

namespace HulaScript::Compilation {
	enum etype {
		CANNOT_PARSE_NUMBER,
		INVALID_CONTROL_CHAR,
		UNEXPECTED_CHAR,
		UNEXPECTED_EOF,
		UNEXPECTED_TOKEN,
		UNEXPECTED_VALUE,
		UNEXPECTED_STATEMENT,
		SYMBOL_NOT_FOUND,
		SYMBOL_ALREADY_EXISTS,
		CANNOT_SET_CAPTURED,
	};

	class error {
	public: 
		error(etype type, source_loc location) : type(type), location(location), msg(std::nullopt) { }
		error(etype type, std::string message, source_loc location) : type(type), location(location), msg(message) { }

		std::string to_print_string();

	private:
		etype type;
		std::optional<std::string> msg;
		source_loc location;
	};
}