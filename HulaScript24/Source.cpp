#include <iostream>
#include <string>
#include <variant>
#include "repl.h"

int main() {
	HulaScript::repl_instance instance(std::nullopt, 256, 16, 256);

	for (;;) {
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