#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <variant>
#include <vector>
#include <map>
#include "sparsepp/spp.h"

#include "tokenizer.h"
#include "instance.h"

namespace HulaScript::Compilation {
	class compiler {
	public:
		compiler(HulaScript::Runtime::instance& instance, bool report_src_locs);

		std::optional<error> compile(tokenizer& tokenizer, bool repl_mode);

		std::optional<uint32_t> declare_global(std::string name) {
			uint64_t hash = str_hash(name.c_str());
			auto it = active_variables.find(hash);
			if (it == active_variables.end()) {
				if (target_instance.global_offset == target_instance.max_globals) {
					return std::nullopt;
				}

				variable_symbol sym = {
					.name = name,
					.is_global = true,
					.local_id = target_instance.global_offset
				};
				active_variables.insert({ hash, sym });
				target_instance.global_offset++;
				return sym.local_id;
			}
			else if (it->second.is_global) {
				return it->second.local_id;
			}
			return std::nullopt;
		}
	private:
		typedef HulaScript::Runtime::instance instance;
		typedef HulaScript::Runtime::instruction instruction;
		typedef HulaScript::Runtime::opcode opcode;

		struct class_declaration {
			std::string name;
			spp::sparse_hash_set<uint64_t> properties;
			spp::sparse_hash_map<uint64_t, uint32_t> methods;

			std::optional<std::pair<uint32_t, uint32_t>> constructor = std::nullopt;
		};

		struct function_declaration {
			std::string name;
			uint32_t max_locals;
			spp::sparse_hash_set<uint64_t> captured_vars;
			std::optional<class_declaration*> class_decl = std::nullopt;
		};

		struct lexical_scope {
			std::vector<uint64_t> symbol_names;
		};

		struct loop_scope {
			uint32_t break_local_count;
			uint32_t continue_local_count;
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

		spp::sparse_hash_map<uint64_t, variable_symbol> active_variables;
		std::vector<lexical_scope> scope_stack;
		std::vector<function_declaration> func_decl_stack;
		std::vector<loop_scope> loop_stack;

		std::vector<uint64_t> declared_toplevel_locals; //locals declared DURING the compilation session; cleared afterwards
		std::vector<uint64_t> declared_globals; //globals declared DURING the compilation session; cleared afterwards
		uint32_t max_instruction;

		instance& target_instance;

		std::optional<error> compile_value(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool expects_statement, bool repl_mode);
		std::optional<error> compile_expression(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, int min_prec, bool repl_mode);
		std::optional<error> compile_statement(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool repl_mode);
		std::optional<error> compile_function(std::string name, tokenizer& tokenizer, std::vector<instruction>& current_section, std::optional<class_declaration*> class_decl, source_loc begin);
		std::optional<error> compile_class(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map);
		std::optional<error> compile_block(tokenizer& tokenizer, std::vector<instruction>& current_section, std::map<uint32_t, source_loc>& ip_src_map, bool(*stop_cond)(token_type));

		void unwind_locals(std::vector<instruction>& instructions, uint32_t probe_ip, bool use_unwind_ins);
		void unwind_loop(uint32_t cond_check_ip, uint32_t finish_ip, std::vector<instruction>& instructions);
		void unwind_error();

		void emit_call_method(std::string method_name, std::vector<instruction>& instructions);

		std::optional<error> validate_symbol_availability(std::string id, std::string symbol_type, source_loc loc);
	};
}