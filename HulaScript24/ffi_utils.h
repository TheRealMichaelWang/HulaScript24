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
		foreign_function(std::string name, std::function<instance::ffi_res_t(value* args, uint32_t arg_c, instance& instance)> func, std::optional<uint32_t> expected_params) : foreign_function(name, func, expected_params, NULL) { }

		foreign_function(std::string name, std::function<instance::ffi_res_t(value* args, uint32_t arg_c, instance& instance)> func, std::optional<uint32_t> expected_params, instance::foreign_resource* captured_resource) : name(name), func(func), expected_params(expected_params), captured_resource(captured_resource) { }

		void release() override {
			if (captured_resource != NULL) {
				captured_resource->unref();
			}
		}

		instance::ffi_res_t invoke(value* args, uint32_t arg_c, instance& instance) override {
			if (expected_params.has_value() && expected_params.value() != arg_c) {
				std::stringstream ss;
				ss << "Foreign function " << name << " expected " << expected_params.value() << " param(s), but got " << arg_c << " argument(s) instead.";
				return instance.make_error(etype::ARGUMENT_COUNT_MISMATCH, ss.str());
			}

			return func(args, arg_c, instance);
		}

	private:
		std::string name;
		std::optional<uint32_t> expected_params;
		std::function<instance::ffi_res_t(value* args, uint32_t arg_c, instance& instance)> func;

		instance::foreign_resource* captured_resource;
	};

	class foreign_object : public instance::foreign_resource {
	protected:
		void register_member(std::string name, std::function<instance::ffi_res_t(value*, uint32_t, instance&)> func, std::optional<uint32_t> expected_params) {
			methods.insert({ hash_combine(str_hash(name.c_str()), vtype::STRING), foreign_function(name, func, expected_params, this) });
		}
	public:
		instance::ffi_res_t load_key(value& key_value, instance& instance) override {
			uint64_t hash = key_value.compute_key_hash();
			auto it = methods.find(hash);
			if (it == methods.end()) {
				return value();
			}
			ref();
			return instance.make_foreign_resource(new foreign_function(it->second));
		}
	private:
		spp::sparse_hash_map<uint64_t, foreign_function> methods;
	};
}