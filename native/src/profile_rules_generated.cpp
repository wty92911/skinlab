// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Recovered pattern recipes; maintained in source for future adaptations.
// Factual pattern/recipe transcription, not a compatibility profile.
#include <skinlab/profile_scan.h>
namespace skinlab { namespace profile {
const char* recovered_dll_sha256() { return "a34d979f110a883b28aa61de6bcb1af5c584b7e15c9be3e85cf0d239b6029990"; }
std::vector<Rule> recovered_rules() { return {
    make_rule("game_client", "48 8B 05 ? ? ? ? 48 8B F2 8B 48 10 81 C1 ? ? ? ? C1 C9 ? 81 F1 ? ? ? ? 83 F9 02", Recipe::RipRelative),
    make_rule("auxiliary_function", "80 3A 00 4C 8B C2 74 11 0F 1F 84 00 00 00 00 00 49 FF C0 41 80 38 00 75 F7 E9", Recipe::CallOrEntry),
    make_rule("local_player", "48 8B 3D ? ? ? ? 48 85 FF 74 15 48 81 C7", Recipe::RipRelative),
    make_rule("hero_list", "48 8B 05 ? ? ? ? 48 8B ? 08 8B 40 ? ? 8D ? ? ? 3B ? 74", Recipe::RipRelative),
    make_rule("champion_manager", "48 8B 0D ? ? ? ? 48 69 D0 ? ? 00 00 48 8B 05", Recipe::RipRelative),
    make_rule("minion_list", "48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 01", Recipe::RipRelative),
    make_rule("turret_list", "48 8B 05 ? ? ? ? 48 8B 58 28 48 85 DB 74 ? 83 43 1C 02", Recipe::RipRelative),
    make_rule("window", "48 8B 0D ? ? ? ? FF 15 ? ? ? ? 48 8B 05 ? ? ? ?", Recipe::RipRelative),
    make_rule("character_data_stack", "48 8D 8D ? ? 00 00 44 8B 8C 24 ? ? 00 00", Recipe::FirstWildcardU32),
    make_rule("skin_id_encoding", "C1 CE ? 48 8D 55 ? 81 F6 ? ? ? ? 48 C7 45 ? 00 00 00 00 C1 CE ? 48 8D 8F ? ? ? ? 89 B7 ? ? ? ?", Recipe::SkinEncoding),
    // 16.22 runtime decoder, independently checked against the current stack.
    make_rule("skin_id_decoding_with_subtract", "8B 9F ? ? ? ? C1 CB ? 81 F3 ? ? ? ? 81 EB ? ? ? ? C1 CB ? 48 8D 8F ? ? ? ? E8 ? ? ? ? 48 8B CE 4C 8B 00 85 DB", Recipe::SkinDecodingWithSubtract),
    make_rule("swap_chain", "48 8D BB ? ? ? ? C6 83 ? ? ? ? ? 0F 84", Recipe::FirstWildcardU32),
    make_rule("character_data_stack_push", "E8 ? ? ? ? 48 8D 8D ? ? 00 00 E8 ? ? ? ? 48 85 C0 74 ? 48 85 ED", Recipe::CallOrEntry),
    make_rule("character_data_stack_update", "88 54 24 10 55 53 56 57 41 54 41 55 41 56 41", Recipe::CallOrEntry),
    make_rule("material_registry_getter", "E8 ? ? ? ? 8B ? 34 45 33 C9", Recipe::CallOrEntry),
    make_rule("translate_string", "E8 ? ? ? ? 45 33 C0 48 8B D0 48 8B CB E8 ? ? ? ? 48", Recipe::CallOrEntry),
    make_rule("gold_redirect_target", "E8 ? ? ? ? 4C 3B ? 0F 94 C0", Recipe::CallOrEntry),
}; }
} }
