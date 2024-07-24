#include <iostream>
#include <string>
#include <variant>
#include "repl.h"

void main() {
	HulaScript::repl_instance instance("myInstance", 1024, 64, 1024);

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

			}
			else if (std::holds_alternative<HulaScript::Compilation::error>(run_res)) {

			}
			else {
				HulaScript::Runtime::value result = std::get<HulaScript::Runtime::value>(run_res);
				std::cout << result.to_print_string() << std::endl;
			}
		}
	}
}