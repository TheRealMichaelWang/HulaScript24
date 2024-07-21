#pragma once

#include <cstdint>

//dj2b str hash
static uint64_t constexpr str_hash(char const* input) {
    return *input ?
        static_cast<uint64_t>(*input) + 33 * str_hash(input + 1) :
        5381;
}

//copied straight from boost
static uint64_t constexpr hash_combine(uint64_t lhs, uint64_t rhs) {
    lhs ^= rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
    return lhs;
}