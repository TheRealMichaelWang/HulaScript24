#include <sstream>
#include "instance.h"

using namespace HulaScript;

instance::error::error(etype type, std::string msg, uint32_t ip) :
 type(type), msg(msg), ip(ip) {

}

instance::error::error(etype type, uint32_t ip) :
	type(type), msg(std::nullopt), ip(ip) {

}

instance::error::error() :
	type(etype::NONE), msg(std::nullopt), ip(0) {

}

instance::error instance::type_error(value::vtype expected, value::vtype got, uint32_t ip) {
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
	ss << "Expecte type " << type_names[expected] << " but got " << type_names[got] << " instead.";

	last_error = error(error::etype::UNEXPECTED_TYPE, ss.str(), ip);
	return last_error;
}

instance::error instance::index_error(double number_index, uint32_t index, uint32_t length, uint32_t ip) {
	std::stringstream ss;
	ss << "Index " << number_index << " is outside the array bounds of [0, " << length << ").";
	
	if (number_index != index) {
		ss << " Index was rounded and transformed into " << index << ".";
	}

	last_error = error(error::etype::UNEXPECTED_TYPE, ss.str(), ip);
	return last_error;
}