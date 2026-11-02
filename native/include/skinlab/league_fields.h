// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef SKINLAB_LEAGUE_FIELDS_H
#define SKINLAB_LEAGUE_FIELDS_H
#include <cstdint>

namespace skinlab { namespace league {
// Recovered structure members, NOT a claim about every future game build.
// Sources: FUN_1800587f0 (stack skin), FUN_1800585f0 (model pointer), fixed
// input DLL a34d979f...9990. Both were observed on live Smolder 16.21.832.5350.
constexpr uint32_t kStackModelPointerOffset=0x18;
constexpr uint32_t kStackSkinOffset=0x38;
constexpr uint32_t kMinimumStackSkinExtent=kStackSkinOffset+sizeof(uint32_t);
} }
#endif
