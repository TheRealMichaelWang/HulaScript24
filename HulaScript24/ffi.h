#pragma once

#include "value.h"

namespace HulaScript::Runtime {
	class foreign_resource
	{
	protected:
		virtual void release() = 0;

		friend class instance;
	};

	class foreign_function : foreign_resource {
	public:
		typedef value(*func_ptr)(value* args, uint32_t arg_c);

		foreign_function(std::string name, std::optional<uint32_t> expected_arguments, func_ptr ptr) : name(name), expected_arguments(expected_arguments), ptr(ptr) { }

	private:
		std::string name;
		std::optional<uint32_t> expected_arguments;
		func_ptr ptr;

		void release() override {};
	};
}