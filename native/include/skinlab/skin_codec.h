// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef SKINLAB_SKIN_CODEC_H
#define SKINLAB_SKIN_CODEC_H
#include <cstdint>
namespace skinlab { namespace codec {
inline uint32_t right(uint32_t value,uint32_t count) {
    count&=31;return count?(value>>count)|(value<<(32-count)):value;
}
inline uint32_t left(uint32_t value,uint32_t count) {
    count&=31;return count?(value<<count)|(value>>(32-count)):value;
}
// kind 0 is the 16.21 encoder. kind 1 inverts the 16.22 decoder:
// ROR(encoded,a), XOR key, SUB add_key, ROR b (all arithmetic modulo 2^32).
inline uint32_t encode(uint32_t skin,uint32_t kind,uint32_t a,uint32_t key,uint32_t b,uint32_t add_key) {
    return kind==1?left((left(skin,b)+add_key)^key,a):right(right(skin,a)^key,b);
}
inline uint32_t decode(uint32_t value,uint32_t kind,uint32_t a,uint32_t key,uint32_t b,uint32_t add_key) {
    return kind==1?right((right(value,a)^key)-add_key,b):left(left(value,b)^key,a);
}
inline bool valid(uint32_t kind,uint32_t a,uint32_t b) {return kind<=1&&a<32&&b<32;}
} }
#endif
