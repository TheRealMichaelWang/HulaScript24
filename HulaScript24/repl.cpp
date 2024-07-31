#include "instructions.h"
#include "repl.h"

using namespace HulaScript;

std::variant<bool, Compilation::error> repl_instance::write_input(std::string input) {
	Compilation::tokenizer tokenizer(input, name);
	
	input_builder << input << "\n\0";
	while (!tokenizer.match_last(Compilation::token_type::END_OF_SOURCE))
	{
		Compilation::token_type type = tokenizer.last_token().type;

		if (!expected_closing_toks.empty() && expected_closing_toks.back() == type) {
			expected_closing_toks.pop_back();
		}
		else {
			switch (type)
			{
			case Compilation::token_type::WHILE:
				expected_closing_toks.push_back(Compilation::token_type::END_BLOCK);
				expected_closing_toks.push_back(Compilation::token_type::DO);
				break;
			case Compilation::token_type::CLASS:
				[[fallthrough]];
			case Compilation::token_type::IF:
				[[fallthrough]];
			case Compilation::token_type::FUNCTION:
				expected_closing_toks.push_back(Compilation::token_type::END_BLOCK);
				break;
			case Compilation::token_type::DO:
				expected_closing_toks.push_back(Compilation::token_type::WHILE);
				break;

			case Compilation::token_type::OPEN_BRACE:
				expected_closing_toks.push_back(Compilation::token_type::CLOSE_BRACE);
				break;
			case Compilation::token_type::OPEN_BRACKET:
				expected_closing_toks.push_back(Compilation::token_type::CLOSE_BRACKET);
				break;
			case Compilation::token_type::OPEN_PAREN:
				expected_closing_toks.push_back(Compilation::token_type::CLOSE_PAREN);
				break;
			}
		}

		auto res = tokenizer.scan_token();
		if (std::holds_alternative<Compilation::error>(res)) {
			expected_closing_toks.clear();
			return std::get<Compilation::error>(res);
		}
	}

	return expected_closing_toks.empty();
}

std::variant<Runtime::value, Runtime::error, Compilation::error> repl_instance::run() {
	Compilation::tokenizer tokenizer(input_builder.str(), name);

	auto compile_res = compiler.compile(tokenizer, true);
	input_builder.str(std::string());
	input_builder.clear();
	if (compile_res.has_value()) {
		return compile_res.value();
	}

	auto run_res = instance.execute();
	if (std::holds_alternative<Runtime::error>(run_res)) {
		return std::get<Runtime::error>(run_res);
	}
	return std::get<Runtime::value>(run_res);
}