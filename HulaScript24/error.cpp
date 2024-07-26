#include <sstream>
#include "error.h"

using namespace HulaScript;

std::string source_loc::to_print_string() {
	std::stringstream ss;

	if (file.has_value()) {
		ss << file.value() << ':';
	}
	ss << row << ':' << column;

	if (function_name.has_value()) {
		ss << " in function " << function_name.value();
	}

	return ss.str();
}

std::string Runtime::error::to_print_string() {
	static const char* err_names[] = {
		"Unexpected Type",
		"Argument Count Mismatch",
		"Out of Memory",
		"Internal Error"
	};

	std::stringstream ss;

	ss << "Runtime Error(" << err_names[type] << ')';
	if (msg.has_value()) {
		ss << ": " << msg.value();
	}

	if (location.has_value()) {
		ss << "\n\t" << location.value().to_print_string();
	}
	else {
		ss << "at ip no. " << ip << '.';
	}

	return ss.str();
}

std::string Compilation::error::to_print_string() {
	static const char* err_names[] = {
		"Cannot Parse Number",
		"Invalid Control Character",
		"Unexpected Character",
		"Unexpected End of Source",
		"Unexpected Token",
		"Unexpected Statement",
		"Unexpected Value",
		"Symbol Not Found",
		"Symbol Already Declared",
		"Cannot Set Captured Variable"
	};

	std::stringstream ss;

	ss << "Compiler Error(" << err_names[type] << ')';
	if (msg.has_value()) {
		ss << ": " << msg.value();
	}
	ss << "\n\t" << location.to_print_string();

	return ss.str();
}