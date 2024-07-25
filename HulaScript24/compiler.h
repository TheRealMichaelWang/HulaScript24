#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <variant>
#include <vector>
#include <set>

#include "tokenizer.h"
#include "instance.h"

namespace HulaScript::Compilation {
	class compiler {
	public:
		compiler();

		std::optional<error> compile(tokenizer& tokenizer, HulaScript::Runtime::instance& target_instance, std::vector<HulaScript::Runtime::instruction>& repl_section, std::vector<HulaScript::Runtime::instruction>& function_section, bool repl_mode);
	private:
		typedef HulaScript::Runtime::instance instance;
		typedef HulaScript::Runtime::instruction instruction;
		typedef HulaScript::Runtime::opcode opcode;

		struct class_declaration {
			std::string name;
			std::vector<std::string> properties;
		};

		struct function_declaration {
			std::string name;
			uint32_t max_locals;
			std::set<std::string> captured_vars;
		};

		struct lexical_scope {
			std::vector<std::string> symbol_names;
		};

		struct loop_scope {
			std::vector<uint32_t> break_requests;
			std::vector<uint32_t> continue_requests;
		};

		struct variable_symbol {
			std::string name;
			bool is_global;
			uint32_t local_id;
			uint32_t func_id;
		};

		bool repl_stop_parsing;
		uint32_t max_globals;

		std::map<std::string, class_declaration> class_decls;
		std::map<std::string, variable_symbol> active_variables;
		std::vector<lexical_scope> scope_stack;
		std::vector<function_declaration> func_decl_stack;
		std::vector<loop_scope> loop_stack;

		std::optional<error> compile_value(tokenizer& tokenizer, instance& target_instance, std::vector<instruction>& current_section, std::vector<instruction>& function_section, bool expects_statement, bool repl_mode);
		std::optional<error> compile_expression(tokenizer& tokenizer, instance& target_instance, std::vector<instruction>& current_section, std::vector<instruction>& function_section, int min_prec, bool repl_mode);
		std::optional<error> compile_statement(tokenizer& tokenizer, instance& target_instance, std::vector<instruction>& current_section, std::vector<instruction>& function_section, bool repl_mode);
		std::optional<error> compile_function(std::string name, tokenizer& tokenizer, instance& target_instance, std::vector<instruction>& current_section, std::vector<instruction>& function_section);
		std::optional<error> compile_block(tokenizer& tokenizer, instance& target_instance, std::vector<instruction>& current_function_section, std::vector<instruction>& function_section, bool(*stop_cond)(token_type));

		void unwind_locals(std::vector<instruction>& instructions, bool use_unwind_ins, bool pop_scope);
		void unwind_loop(uint32_t cond_check_ip, uint32_t finish_ip, std::vector<instruction>& instructions);

		std::optional<error> validate_symbol_availability(std::string id, std::string symbol_type, source_loc loc);
	};
}