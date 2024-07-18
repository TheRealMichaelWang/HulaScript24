#pragma once

#include <cstdint>

static uint32_t constexpr str_hash(char const* input) {
    return *input ?
        static_cast<uint32_t>(*input) + 33 * str_hash(input + 1) :
        5381;
}