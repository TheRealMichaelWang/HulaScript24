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

		std::string value_to_print_str(Runtime::value& value) {
			return instance.value_to_print_str(value);
		}

		bool declare_global(std::string name, Runtime::value value) {
			auto res = compiler.declare_global(name);
			if (!res.has_value()) {
				return false;
			}
			instance.set_global(res.value(), value);
			return true;
		}

		bool declare_func(std::string name, Runtime::foreign_function_t func) {
			return declare_global(name, Runtime::value(Runtime::vtype::FOREIGN_FUNCTION, 0, static_cast<void*>(func)));
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