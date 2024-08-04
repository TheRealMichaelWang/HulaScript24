#pragma once

#include <variant>
#include "sparsepp/spp.h"
#include "error.h"
#include "value.h"

namespace HulaScript::Runtime {
	typedef std::variant<value, error>(*foreign_function)(value* args, uint32_t arg_c);

	class foreign_resource
	{
	protected:
		virtual void release() = 0;

		friend class instance;
	private:
		spp::sparse_hash_map<uint64_t, foreign_function> methods;

		friend class instance;
	};
}