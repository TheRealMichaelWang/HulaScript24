#pragma once

#include <vector>
#include <variant>
#include <string>
#include <memory>
#include "instance.h"
#include "compiler.h"
#include "ffi_utils.h"

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

		bool declare_func(std::string name, Runtime::instance::ffi_res_t(*func)(Runtime::value*, uint32_t), std::optional<uint32_t> expected_params) {
			return declare_global(name, instance.make_foreign_resource(std::shared_ptr<Runtime::instance::foreign_resource>(new Runtime::foreign_function(name, func, instance, expected_params))));
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