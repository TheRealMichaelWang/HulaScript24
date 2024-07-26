#include <iostream>
#include <string>
#include <variant>
#include "repl.h"

int main() {
	HulaScript::repl_instance instance("myInstance", 1024, 64, 1024);

	std::cout << ">>> ";
	for (;;) {
		std::string line;
		std::getline(std::cin, line);

		auto input_res = instance.write_input(line);
		if (std::holds_alternative<HulaScript::Compilation::error>(input_res)) {
			//handle error
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
				std::cout << result.to_print_string() << std::endl;
			}
			std::cout << ">>> ";
		}
		else {
			std::cout << "... ";
		}
	}

	return 0;
}