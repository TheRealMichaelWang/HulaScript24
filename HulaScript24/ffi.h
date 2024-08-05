#pragma once

#include <variant>
#include <string>
#include <functional>
#include "error.h"
#include "value.h"

namespace HulaScript::Runtime {
	typedef std::variant<value, error> ffi_res_t;

	class foreign_resource
	{
	public:
		virtual void release() { }

		virtual ffi_res_t load_key(value& key_value) { return value(); }
		virtual ffi_res_t set_key(value& key_value, value& type_value) { return value(); }

		virtual ffi_res_t invoke(value* args, uint32_t arg_c) { return value(); }
	};
}