#include "instance.h"

void main() {
	HulaScript::Runtime::instance instance(1024, 64, 1024);

	instance.run("a = 1 + 2", std::nullopt);
}