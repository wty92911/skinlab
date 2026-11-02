// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef SKINLAB_PROFILE_SCAN_H
#define SKINLAB_PROFILE_SCAN_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// This is an observation library, not a game binding or a function caller.
// A caller must keep its module reference alive throughout collection/reread.
namespace skinlab { namespace profile {
enum class Recipe { RipRelative, CallOrEntry, FirstWildcardU32, SkinEncoding };
enum class State { Missing, Ambiguous, Incomplete, Invalid, Changed, ReadFailed,
                   SnapshotOnly, RereadEqual };
struct Rule {
    std::string name;
    Recipe recipe;
    std::vector<uint8_t> bytes, fixed;
    size_t anchor_start=0, anchor_size=0, first_wildcard=0;
};
Rule make_rule(const char* name, const char* pattern, Recipe recipe);
std::vector<Rule> recovered_rules();
const char* recovered_dll_sha256();
const char* state_name(State state);

struct Range { uint32_t rva=0, size=0; bool readable=false, executable=false; };
struct Region { uint32_t rva=0; std::vector<uint8_t> bytes; };
struct Gap { uint32_t rva=0, size=0, error=0; };
struct Snapshot {
    uintptr_t module_base=0;
    uint32_t image_size=0;
    // Current committed page ranges for candidate admission; not a lifetime lock.
    std::vector<Range> ranges;
    // Only executable, readable image-section bytes are copied/scanned.
    std::vector<Region> regions;
    std::vector<Gap> gaps;
    uint64_t expected_bytes=0, copied_bytes=0, elapsed_ms=0;
    bool complete=false;
    std::string error;
};
struct Candidate {
    uint32_t rva=0, field_offset=0, xor_key=0;
    uint8_t rotate_first=0, rotate_second=0;
};
struct Result {
    std::string name, reason;
    State state=State::Missing;
    // At most two matches are retained. Two means "at least two", not an exact count.
    std::vector<uint32_t> matches;
    bool match_count_capped=false, has_candidate=false;
    Candidate candidate;
};
using ReadRva = bool (*)(void*, uint32_t, void*, size_t);
// Validates snapshot bounds, nonoverlap and a 128 MiB cap. Deadline expiry or any
// acquisition gap prevents a candidate from being published. Reread checks the
// full matched bytes including wildcards; equal bytes still do NOT prove safe
// engine calls, object lifetime, thread affinity or multi-version compatibility.
std::vector<Result> analyze(const Snapshot&, const std::vector<Rule>&,
                            ReadRva=nullptr, void* context=nullptr,
                            uint32_t max_scan_ms=5000);
// Windows only: current-process module, no OpenProcess or remote process option.
// No engine function is called, and no target bytes/protections are modified.
Snapshot collect_current_module(void* module, uint32_t max_collect_ms=5000);
bool read_current_rva(void* snapshot, uint32_t rva, void* output, size_t size);
} }
#endif
