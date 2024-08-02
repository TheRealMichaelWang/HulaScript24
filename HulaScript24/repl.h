#pragma once

#include <vector>
#include <variant>
#include <string>
#include "instance.h"
#include "compiler.h"

namespace HulaScript {
	class repl_instance {
	public:
		repl_instance(std::optional<std::string> name, uint32_t max_locals, uint32_t max_globals, size_t max_table) : name(name), instance(max_locals, max_globals, max_table), compiler(instance, true), eval_no(0) { }

		//input is a piece of the source. The function will return when the source is complete enough for evaluation
		std::variant<bool, Compilation::error> write_input(std::string input);

		std::variant<Runtime::value, Runtime::error, Compilation::error> run();

		std::string error_to_print_str(Runtime::error& error) {
			return instance.error_to_print_str(error);
		}

		std::string value_to_print_str(Runtime::value& value) {
			return instance.value_to_print_str(value);
		}
	private:
		Runtime::instance instance;
		Compilation::compiler compiler;
		std::optional<std::string> name;

		std::string input_builder;
		uint32_t eval_no;
		std::vector<Compilation::token_type> expected_closing_toks;
	};
}