// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef SKINLAB_LUX_FORMS_H
#define SKINLAB_LUX_FORMS_H
#include <cstdint>
// CharacterRecord names and skin 7 confirmed from the shipped Lux WAD.
// These are model aliases, not inferred runtime gear integers.
constexpr const char* sl_lux_forms[]={"Lux","LuxAir","LuxDark","LuxFire","LuxIce",
    "LuxMagma","LuxMystic","LuxNature","LuxStorm","LuxWater"};
constexpr uint32_t sl_lux_form_count=sizeof(sl_lux_forms)/sizeof(sl_lux_forms[0]);
#endif
