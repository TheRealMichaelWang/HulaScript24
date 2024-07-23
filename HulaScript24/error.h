#pragma once

#include <cstdint>
#include <string>
#include <optional>

namespace HulaScript::Runtime {
	enum etype {
		UNEXPECTED_TYPE,
		ARGUMENT_COUNT_MISMATCH,
		MEMORY,
		INTERNAL_ERROR
	};

	class error {
	public:
		etype type;

		error(error::etype type, std::string message, uint32_t ip) : type(type), msg(msg), ip(ip) { }
		error(etype type, uint32_t ip) : type(type), msg(std::nullopt), ip(ip) { }
	private:
		std::optional<std::string> msg;
		uint32_t ip;
	};
}

namespace HulaScript::Compilation {
	class source_loc {
	public:
		source_loc(size_t row, size_t col, std::optional<std::string> file) : row(row), column(col), file(file) { }
		source_loc(std::optional<std::string> file) : row(1), column(1), file(file) { }
	private:
		size_t row;
		size_t column;
		std::optional<std::string> file;
	};

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
		etype type;

		error(etype type, source_loc location) : type(type), location(location), msg(std::nullopt) { }
		error(etype type, std::string message, source_loc location) : type(type), location(location), msg(msg) { }

	private:
		std::optional<std::string> msg;
		source_loc location;
	};
}