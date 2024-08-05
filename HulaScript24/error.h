#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <vector>

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
		INTERNAL_ERROR,
		FOREIGN_RESOURCE
	};

	class error {
	public:
		error(error::etype type, std::optional<std::string> message, std::optional<source_loc> location, std::vector<std::pair<std::optional<source_loc>, uint32_t>> stack_trace) : type(type), msg(message), stack_trace(stack_trace), location(location) { }

		std::string to_print_string();
	private:
		etype type;
		std::optional<std::string> msg;
		std::optional<source_loc> location;
		std::vector<std::pair<std::optional<source_loc>, uint32_t>> stack_trace;
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
		CANNOT_CAPTURE_VAR
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