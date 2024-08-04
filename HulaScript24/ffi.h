#pragma once

#include <variant>
#include <string>
#include "sparsepp/spp.h"
#include "error.h"
#include "value.h"
#include "hash.h"

namespace HulaScript::Runtime {
	typedef std::variant<value, error>(*foreign_function)(value* args, uint32_t arg_c);

	class foreign_resource
	{
	public:
		virtual void release() = 0;

		virtual value load_key(value& key_value) = 0;
		virtual value set_key(value& key_value, value& type_value) = 0;
	};

	class foreign_object : public foreign_resource {
	protected:
		void register_function(std::string name, foreign_function func) {
			uint64_t hash = hash_combine(str_hash(name.c_str()), vtype::STRING);
			methods.insert({ hash, func });
		}
	public:
		value load_key(value& str_key) override {
			auto it = methods.find(str_key.compute_hash());
			if (it == methods.end()) {
				return value();
			}
			return value(vtype::FOREIGN_FUNCTION, 0, it->second);
		}

		value set_key(value& str_key, value& value_key) override {
			return value();
		}
	private:
		spp::sparse_hash_map<uint64_t, foreign_function> methods;
	};
}