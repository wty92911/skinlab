// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <skinlab/api.h>
#include <cstring>
#include <limits>
#include <new>

static_assert(sizeof(void*) == 8, "SkinLab requires a 64-bit build");

namespace {
struct Snapshot { uint32_t encoded; uint32_t skin; };
bool same(const Snapshot& left, const Snapshot& right) noexcept {
    return left.encoded == right.encoded && left.skin == right.skin;
}
uint32_t rotate_right(uint32_t value, uint32_t count) noexcept {
    count &= 31;
    return count ? (value >> count) | (value << (32 - count)) : value;
}
uint32_t rotate_left(uint32_t value, uint32_t count) noexcept {
    count &= 31;
    return count ? (value << count) | (value >> (32 - count)) : value;
}
uint32_t SL_CALL encode(uint32_t skin, uint32_t a, uint32_t key, uint32_t b) noexcept {
    return rotate_right(rotate_right(skin, a) ^ key, b);
}
uint32_t SL_CALL decode(uint32_t value, uint32_t a, uint32_t key, uint32_t b) noexcept {
    return rotate_left(rotate_left(value, b) ^ key, a);
}
const char* SL_CALL status_text(SlStatus status) noexcept {
    switch (status) {
    case SL_OK: return "ok";
    case SL_INVALID_ARGUMENT: return "invalid argument";
    case SL_ABI_MISMATCH: return "ABI version mismatch";
    case SL_UNSUPPORTED_BINDING: return "live game binding is not verified";
    case SL_LAYOUT_OUT_OF_RANGE: return "layout outside object span or overlapping fields";
    case SL_INVALID_SKIN: return "skin is not in the host catalog";
    case SL_TARGET_CHANGED: return "target was destroyed or replaced";
    case SL_INCONSISTENT_STATE: return "encoded and stack skin values disagree";
    case SL_UPDATE_FAILED: return "refresh did not commit requested skin; previous state restored";
    case SL_RECOVERY_REQUIRED: return "refresh/rollback incomplete; retry restore";
    case SL_WRONG_THREAD: return "operation must run on the creating thread";
    case SL_BUSY: return "reentrant operation rejected";
    case SL_ALLOCATION_FAILED: return "allocation failed";
    }
    return "unknown status";
}
}

struct SlRuntime {
    SlConfig config;
    DWORD owner_thread;
    bool busy = false;
    bool active = false;
    bool recovery = false;
    Snapshot original{};
    size_t encoded_offset;
    size_t stack_skin_offset;

    explicit SlRuntime(const SlConfig& value) noexcept
        : config(value), owner_thread(GetCurrentThreadId()),
          encoded_offset(value.layout.object_skin_offset),
          stack_skin_offset(size_t(value.layout.character_stack_offset) + value.layout.stack_skin_offset) {}

    SlStatus check_target() const noexcept {
        return config.generation(config.context) == config.expected_generation
            ? SL_OK : SL_TARGET_CHANGED;
    }
    Snapshot read() const noexcept {
        Snapshot value{};
        const auto* data = static_cast<const unsigned char*>(config.object_data);
        std::memcpy(&value.encoded, data + encoded_offset, sizeof(value.encoded));
        std::memcpy(&value.skin, data + stack_skin_offset, sizeof(value.skin));
        return value;
    }
    void write(const Snapshot& value) noexcept {
        auto* data = static_cast<unsigned char*>(config.object_data);
        std::memcpy(data + encoded_offset, &value.encoded, sizeof(value.encoded));
        std::memcpy(data + stack_skin_offset, &value.skin, sizeof(value.skin));
    }
    uint32_t decoded(const Snapshot& value) const noexcept {
        const auto& layout = config.layout;
        return decode(value.encoded, layout.rotation_a, layout.xor_key, layout.rotation_b);
    }
    bool refresh() const noexcept {
        auto* stack = static_cast<unsigned char*>(config.object_data) + config.layout.character_stack_offset;
        return config.refresh(config.context, stack, 1) != 0;
    }
    void log(const char* event, SlStatus status) const noexcept {
        if (config.log) config.log(config.context, event, status);
    }
};

namespace {
struct Operation {
    SlRuntime* runtime;
    SlStatus status;
    explicit Operation(SlRuntime* value) noexcept : runtime(nullptr), status(SL_OK) {
        if (!value) status = SL_INVALID_ARGUMENT;
        else if (value->owner_thread != GetCurrentThreadId()) status = SL_WRONG_THREAD;
        else if (value->busy) status = SL_BUSY;
        else { runtime = value; runtime->busy = true; }
    }
    ~Operation() { if (runtime) runtime->busy = false; }
};

SlStatus SL_CALL create(const SlConfig* config, SlRuntime** output) noexcept {
    if (!output || *output || !config || config->struct_size < sizeof(SlConfig)) return SL_INVALID_ARGUMENT;
    if (config->abi_version != SL_ABI_VERSION) return SL_ABI_MISMATCH;
    // A real adapter needs a verified runtime profile and loading path first.
    // Accepting guessed game offsets here would misrepresent compatibility.
    if (config->binding_kind != SL_BINDING_LOCAL_FIXTURE && config->binding_kind != SL_BINDING_LEAGUE_EXPERIMENT) return SL_UNSUPPORTED_BINDING;
    if (!config->object_data || !config->expected_generation || !config->generation ||
        !config->skin_allowed || !config->refresh) return SL_INVALID_ARGUMENT;
    const uint64_t encoded = config->layout.object_skin_offset;
    const uint64_t stack = uint64_t(config->layout.character_stack_offset) + config->layout.stack_skin_offset;
    const uint64_t end = (encoded > stack ? encoded : stack) + sizeof(uint32_t);
    const uintptr_t address = reinterpret_cast<uintptr_t>(config->object_data);
    if (end > config->object_size || end > std::numeric_limits<uintptr_t>::max() - address ||
        (encoded < stack + 4 && stack < encoded + 4)) return SL_LAYOUT_OUT_OF_RANGE;
    auto* runtime = new(std::nothrow) SlRuntime(*config);
    if (!runtime) return SL_ALLOCATION_FAILED;
    SlStatus status = runtime->check_target();
    if (status == SL_OK) {
        const Snapshot initial = runtime->read();
        if (runtime->decoded(initial) != initial.skin) status = SL_INCONSISTENT_STATE;
    }
    if (status != SL_OK) { delete runtime; return status; }
    *output = runtime;
    runtime->busy = true;
    runtime->log("runtime.created", SL_OK);
    runtime->busy = false;
    return SL_OK;
}

SlStatus SL_CALL state(SlRuntime* runtime, SlState* output) noexcept {
    Operation operation(runtime);
    if (operation.status != SL_OK) return operation.status;
    if (!output || output->struct_size < sizeof(SlState)) return SL_INVALID_ARGUMENT;
    const SlStatus status = runtime->check_target();
    if (status != SL_OK) return status;
    const Snapshot current = runtime->read();
    output->flags = (runtime->active ? SL_STATE_ACTIVE : 0) | (runtime->recovery ? SL_STATE_RECOVERY_REQUIRED : 0);
    output->current_skin = static_cast<int32_t>(runtime->decoded(current));
    output->current_encoded_skin = current.encoded;
    output->current_stack_skin = current.skin;
    output->original_skin = runtime->active ? static_cast<int32_t>(runtime->decoded(runtime->original)) : output->current_skin;
    output->generation = runtime->config.expected_generation;
    return SL_OK;
}

SlStatus SL_CALL apply(SlRuntime* runtime, int32_t skin_id) noexcept {
    Operation operation(runtime);
    if (operation.status != SL_OK) return operation.status;
    if (runtime->recovery) return SL_RECOVERY_REQUIRED;
    SlStatus status = runtime->check_target();
    if (status != SL_OK) return status;
    if (skin_id < 0 || !runtime->config.skin_allowed(runtime->config.context, skin_id)) return SL_INVALID_SKIN;
    status = runtime->check_target();
    if (status != SL_OK) return status;
    const Snapshot before = runtime->read();
    if (runtime->decoded(before) != before.skin) return SL_INCONSISTENT_STATE;
    if (before.skin == static_cast<uint32_t>(skin_id)) return SL_OK;
    const bool was_active = runtime->active;
    if (!was_active) runtime->original = before;
    runtime->active = true;
    const auto& layout = runtime->config.layout;
    const Snapshot desired{encode(static_cast<uint32_t>(skin_id), layout.rotation_a, layout.xor_key, layout.rotation_b), static_cast<uint32_t>(skin_id)};
    runtime->write(desired);
    const bool refreshed = runtime->refresh();
    if (runtime->check_target() != SL_OK) {
        runtime->recovery = true;
        runtime->log("skin.target_lost_during_refresh", SL_RECOVERY_REQUIRED);
        return SL_RECOVERY_REQUIRED;
    }
    if (refreshed && same(runtime->read(), desired)) {
        runtime->log("skin.applied", SL_OK);
        return SL_OK;
    }
    runtime->write(before);
    if (!runtime->refresh() || runtime->check_target() != SL_OK || !same(runtime->read(), before)) {
        runtime->recovery = true;
        runtime->log("skin.rollback_refresh_failed", SL_RECOVERY_REQUIRED);
        return SL_RECOVERY_REQUIRED;
    }
    runtime->active = was_active;
    runtime->log("skin.apply_rolled_back", SL_UPDATE_FAILED);
    return SL_UPDATE_FAILED;
}

SlStatus restore_impl(SlRuntime* runtime) noexcept {
    if (!runtime->active && !runtime->recovery) return SL_OK;
    const SlStatus status = runtime->check_target();
    if (status != SL_OK) return status;
    runtime->write(runtime->original);
    if (!runtime->refresh() || runtime->check_target() != SL_OK || !same(runtime->read(), runtime->original)) {
        runtime->recovery = true;
        runtime->log("skin.restore_refresh_failed", SL_RECOVERY_REQUIRED);
        return SL_RECOVERY_REQUIRED;
    }
    runtime->active = false;
    runtime->recovery = false;
    runtime->log("skin.restored", SL_OK);
    return SL_OK;
}
SlStatus SL_CALL restore(SlRuntime* runtime) noexcept {
    Operation operation(runtime);
    return operation.status == SL_OK ? restore_impl(runtime) : operation.status;
}
SlStatus SL_CALL destroy(SlRuntime** pointer) noexcept {
    if (!pointer) return SL_INVALID_ARGUMENT;
    if (!*pointer) return SL_OK;
    auto* runtime = *pointer;
    {
        Operation operation(runtime);
        if (operation.status != SL_OK) return operation.status;
        const SlStatus status = restore_impl(runtime);
        if (status != SL_OK) return status;
    }
    delete runtime;
    *pointer = nullptr;
    return SL_OK;
}
SlStatus SL_CALL abandon_destroyed_target(SlRuntime** pointer) noexcept {
    if (!pointer) return SL_INVALID_ARGUMENT;
    if (!*pointer) return SL_OK;
    auto* runtime = *pointer;
    {
        Operation operation(runtime);
        if (operation.status != SL_OK) return operation.status;
        if (runtime->check_target() != SL_TARGET_CHANGED) return SL_INVALID_ARGUMENT;
        runtime->log("runtime.destroyed_target_abandoned", SL_TARGET_CHANGED);
    }
    delete runtime;
    *pointer = nullptr;
    return SL_OK;
}
}

extern "C" __declspec(dllexport) SlStatus SL_CALL skinlab_get_api(uint32_t requested, SlApi* output) noexcept {
    if (!output || output->struct_size < sizeof(SlApi)) return SL_INVALID_ARGUMENT;
    if (requested != SL_ABI_VERSION) return SL_ABI_MISMATCH;
    const SlApi api{sizeof(SlApi), SL_ABI_VERSION, status_text, encode, decode, create, state, apply, restore, destroy, abandon_destroyed_target};
    *output = api;
    return SL_OK;
}

#ifndef SKINLAB_EMBEDDED_CORE
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    // Leave normal CRT thread notifications intact. Core/runtime work starts
    // only through explicit API calls after loader initialization returns.
    return TRUE;
}
#endif
