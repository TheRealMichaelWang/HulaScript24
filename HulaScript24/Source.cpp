#include "instance.h"

void main() {
	HulaScript::instance instance(1024, 64, 1024);

	HulaScript::instance::value val = instance.make_number(5);
}