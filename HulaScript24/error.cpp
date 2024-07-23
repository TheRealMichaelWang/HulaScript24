#include <sstream>
#include "instance.h"

using namespace HulaScript::Runtime;

error instance::type_error(vtype expected, vtype got, uint32_t ip) {
	static const char* type_names[] = {
		"dictionary",
		"closure",
		"object/class",
		"array",
		"string",
		"number",
		"nil"
	};

	std::stringstream ss;
	ss << "Expected type " << type_names[expected] << " but got " << type_names[got] << " instead.";

	return error(etype::UNEXPECTED_TYPE, ss.str(), ip);
}

error instance::index_error(double number_index, uint32_t index, uint32_t length, uint32_t ip) {
	std::stringstream ss;
	ss << "Index " << number_index << " is outside the array bounds of [0, " << length << ").";
	
	if (number_index != index) {
		ss << " Index was rounded and transformed into " << index << ".";
	}

	return error(etype::UNEXPECTED_TYPE, ss.str(), ip);
}