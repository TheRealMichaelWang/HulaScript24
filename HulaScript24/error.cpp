#include <sstream>
#include "error.h"
#include "instance.h"

using namespace HulaScript;

std::string source_loc::to_print_string() {
	std::stringstream ss;

	if (file.has_value()) {
		ss << file.value() << ": ";
	}
	ss << "row " << row << ", col " << column;

	if (function_name.has_value()) {
		ss << " in function " << function_name.value();
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
		"Cannot Set Captured Variable",
		"Cannot Capture Variable"
	};

	std::stringstream ss;

	ss << "Compiler Error(" << err_names[type] << ')';
	if (msg.has_value()) {
		ss << ": " << msg.value();
	}
	ss << "\n\t" << location.to_print_string();

	return ss.str();
}

std::string Runtime::instance::error_to_print_str(Runtime::error& error) {
	static const char* err_names[] = {
		"Unexpected Type",
		"Argument Count Mismatch",
		"Out of Memory",
		"Internal Error"
	};

	std::stringstream ss;

	ss << "Runtime Error(" << err_names[error.type] << ')';
	auto location = loc_from_ip(error.ip);
	if (location.has_value()) {
		ss << " at " << location.value().to_print_string();
	}
	else {
		ss << " at ip no. " << error.ip;
	}

	if (error.msg.has_value()) {
		ss << ": " << error.msg.value();
	}

	for (auto it = error.stack_trace.begin(); it != error.stack_trace.end(); ) {
		ss << "\n\t";

		uint32_t ip = *it;
		location = loc_from_ip(ip);
		if (location.has_value()) {
			ss << location.value().to_print_string();
		}
		else {
			ss << "ip no. " << ip;
		}

		uint32_t repeats = 0;
		do {
			it++;
			repeats++;
		} while (it != error.stack_trace.end() && ip == *it);

		if (repeats > 1) {
			ss << "(" << repeats << " times)";
		}
	}

	return ss.str();
}