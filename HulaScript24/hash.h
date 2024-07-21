#pragma once

#include <cstdint>

static uint64_t constexpr str_hash(char const* input) {
    return *input ?
        static_cast<uint64_t>(*input) + 33 * str_hash(input + 1) :
        5381;
}