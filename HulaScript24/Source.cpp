#include <iostream>
#include <string>
#include <variant>
#include "repl.h"

using HulaScript::Runtime::value;
using HulaScript::Runtime::instance;

class range_obj : public HulaScript::Runtime::foreign_object {
public:
	range_obj(int i, int max, int step) : i(i), max(max), step(step) {
		assert(i < max);
		assert((max - i) % step == 0);

		register_member("elem", [this](value* args, uint32_t argc, HulaScript::Runtime::instance& instance) -> instance::result_t {
			return value((double)this->i);
		}, 0);
		register_member("next", [this](value* args, uint32_t argc, HulaScript::Runtime::instance& instance) -> instance::result_t {
			this->i += this->step;
			if (this->i == this->max) {
				return value();
			}
			return instance.make_foreign_resource(this);
		}, 0);
	}
private:
	int i;
	int max;
	int step;
};

int main() {
	static bool stop = false;
	HulaScript::repl_instance instance(std::nullopt, 256, 16, 256);
	instance.declare_func("range", [](value* args, uint32_t arg_c, HulaScript::Runtime::instance& instance) -> instance::result_t {
		if (args[0].number() >= args[1].number()) {
			return value();
		}
		//use new because make_shared copies the value, which causes members that capture this to be invalid
		return instance.make_foreign_resource(new range_obj(args[0].number(), args[1].number(), args[2].number()));
	}, 3);
	instance.declare_func("print", [](value* args, uint32_t arg_c, HulaScript::Runtime::instance& instance)->instance::result_t {
		for (uint_fast32_t i = 0; i < arg_c; i++) {
			std::cout << instance.value_to_print_str(args[i]);
		}
		std::cout << std::endl;
		return value();
	}, std::nullopt);
	instance.declare_func("stop", [](value* args, uint32_t arg_c, HulaScript::Runtime::instance& instance)->instance::result_t {
		stop = true;
		return value();
	}, 0);


	while (!stop) {
		char buf[256];
		//std::getline(std::cin, line);
		std::cin.getline(buf, 256);

		auto input_res = instance.write_input(std::string(buf));
		if (std::holds_alternative<HulaScript::Compilation::error>(input_res)) {
			auto compilation_err = std::get<HulaScript::Compilation::error>(input_res);
			std::cout << compilation_err.to_print_string() << std::endl;
		}
		else if(std::get<bool>(input_res)) {
			auto run_res = instance.run();
			if (std::holds_alternative<HulaScript::Runtime::error>(run_res)) {
				auto runtime_err = std::get<HulaScript::Runtime::error>(run_res);
				std::cout << runtime_err.to_print_string() << std::endl;
			}
			else if (std::holds_alternative<HulaScript::Compilation::error>(run_res)) {
				auto compilation_err = std::get<HulaScript::Compilation::error>(run_res);
				std::cout << compilation_err.to_print_string() << std::endl;
			}
			else {
				HulaScript::Runtime::value result = std::get<HulaScript::Runtime::value>(run_res);
				std::cout << instance.value_to_print_str(result) << std::endl;
			}
		}
	}

	return 0;
}
