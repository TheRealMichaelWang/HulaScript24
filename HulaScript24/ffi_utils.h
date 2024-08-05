#pragma once

#include <sstream>
#include <vector>
#include <memory>
#include "sparsepp/spp.h"

#include "instance.h"
#include "hash.h"

namespace HulaScript::Runtime {
	class foreign_function : public instance::foreign_resource {
	public:
		foreign_function(std::string name, std::function<instance::ffi_res_t(value* args, uint32_t arg_c)> func, instance& instance, std::optional<uint32_t> expected_params) : name(name), func(func), instance(instance), expected_params(expected_params) { }

		instance::ffi_res_t invoke(value* args, uint32_t arg_c) override {
			if (expected_params.has_value() && expected_params.value() != arg_c) {
				std::stringstream ss;
				ss << "Foreign function " << name << " expected " << expected_params.value() << " param(s), but got " << arg_c << " argument(s) instead.";
				return instance.make_error(etype::ARGUMENT_COUNT_MISMATCH, ss.str());
			}

			return func(args, arg_c);
		}

	private:
		std::string name;
		std::optional<uint32_t> expected_params;
		std::function<instance::ffi_res_t(value* args, uint32_t arg_c)> func;
		instance& instance;
	};

	class foreign_object : public instance::foreign_resource {
	protected:
		foreign_object(instance& instance) : instance(instance) { }

		void register_function(std::string name, std::function<instance::ffi_res_t(value*, uint32_t)> func, std::optional<uint32_t> expected_params) {
			methods.insert({ hash_combine(str_hash(name.c_str()), vtype::STRING), std::make_shared<foreign_function>(foreign_function(name, func, instance, expected_params)) });
		}
	public:
		instance::ffi_res_t load_key(value& key_value) override {
			uint64_t hash = key_value.compute_key_hash();
			auto it = methods.find(hash);
			if (it == methods.end()) {
				return value();
			}
			return instance.make_foreign_resource(std::static_pointer_cast<instance::foreign_resource>(it->second));
		}
	private:
		spp::sparse_hash_map<uint64_t, std::shared_ptr<foreign_function>> methods;
		instance& instance;
	};
}