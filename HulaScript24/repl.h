#pragma once

#include <sstream>
#include <vector>
#include <variant>
#include "instance.h"
#include "compiler.h"

namespace HulaScript {
	class repl_instance {
	public:
		repl_instance(std::optional<std::string> name, uint32_t max_locals, uint32_t max_globals, size_t max_table) : name(name), instance(max_locals, max_globals, max_table), compiler(instance, true) { }

		//input is a piece of the source. The function will return when the source is complete enough for evaluation
		std::variant<bool, Compilation::error> write_input(std::string input);

		std::variant<Runtime::value, Runtime::error, Compilation::error> run();
	private:
		Runtime::instance instance;
		Compilation::compiler compiler;
		std::optional<std::string> name;

		std::stringstream input_builder;
		std::vector<Compilation::token_type> expected_closing_toks;
	};
}