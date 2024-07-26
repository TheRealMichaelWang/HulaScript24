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
		compiler(HulaScript::Runtime::instance& instance, bool report_src_locs);

		std::optional<error> compile(tokenizer& tokenizer, bool repl_mode);
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
		bool report_src_locs;
		uint32_t max_globals;

		std::map<std::string, class_declaration> class_decls;
		std::map<std::string, variable_symbol> active_variables;
		std::vector<lexical_scope> scope_stack;
		std::vector<function_declaration> func_decl_stack;
		std::vector<loop_scope> loop_stack;

		std::vector<std::string> declared_toplevel_locals; //locals declared DURING the compilation session; cleared afterwards
		std::vector<std::string> declared_globals; //globals declared DURING the compilation session; cleared afterwards
		uint32_t max_instruction;

		instance& target_instance;

		std::optional<error> compile_value(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool expects_statement, bool repl_mode);
		std::optional<error> compile_expression(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, int min_prec, bool repl_mode);
		std::optional<error> compile_statement(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool repl_mode);
		std::optional<error> compile_function(std::string name, tokenizer& tokenizer, std::vector<instruction>& current_section);
		std::optional<error> compile_block(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool(*stop_cond)(token_type));

		void unwind_locals(std::vector<instruction>& instructions, bool use_unwind_ins);
		void unwind_loop(uint32_t cond_check_ip, uint32_t finish_ip, std::vector<instruction>& instructions);
		void unwind_error();

		std::optional<error> validate_symbol_availability(std::string id, std::string symbol_type, source_loc loc);
	};
}