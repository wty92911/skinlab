#ifndef SKINLAB_API_H
#define SKINLAB_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define SL_CALL __cdecl
#else
#define SL_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum { SL_ABI_VERSION = 1 };
typedef enum SlStatus {
    SL_OK = 0,
    SL_INVALID_ARGUMENT,
    SL_ABI_MISMATCH,
    SL_UNSUPPORTED_BINDING,
    SL_LAYOUT_OUT_OF_RANGE,
    SL_INVALID_SKIN,
    SL_TARGET_CHANGED,
    SL_INCONSISTENT_STATE,
    SL_UPDATE_FAILED,
    SL_RECOVERY_REQUIRED,
    SL_WRONG_THREAD,
    SL_BUSY,
    SL_ALLOCATION_FAILED
} SlStatus;

typedef enum SlBindingKind {
    SL_BINDING_LOCAL_FIXTURE = 1,
    SL_BINDING_LEAGUE = 2,
    /* Explicit experimental adapter; not a general compatibility attestation.
     * Caller must validate code/profile, object lifetime and calling thread. */
    SL_BINDING_LEAGUE_EXPERIMENT = 3
} SlBindingKind;

/* Confirmed algorithm, not confirmed live offsets. All offsets address bytes
 * inside the caller-owned object span. The stack is embedded in that object.
 * Evidence: native-profile-functions/0x1800587f0.c.txt. */
typedef struct SlLayout {
    uint32_t object_skin_offset;
    uint32_t character_stack_offset;
    uint32_t stack_skin_offset;
    uint32_t rotation_a;
    uint32_t xor_key;
    uint32_t rotation_b;
} SlLayout;

/* Callbacks run synchronously on the creating thread and must not throw.
 * The creating thread, context and object storage must outlive the runtime.
 * The host serializes storage/generation changes on that thread, and grants
 * this transaction exclusive ownership of the two skin fields while active.
 * A new object must not reuse a live runtime's generation. Generation checks
 * are an identity guard, NOT a lifetime lock or a concurrency guarantee. */
typedef uint64_t (SL_CALL *SlGeneration)(void* context);
typedef int (SL_CALL *SlSkinAllowed)(void* context, int32_t skin_id);
typedef int (SL_CALL *SlRefresh)(void* context, void* character_stack, int force);
typedef void (SL_CALL *SlLog)(void* context, const char* event, SlStatus status);

typedef struct SlConfig {
    uint32_t struct_size;
    uint32_t abi_version;
    SlBindingKind binding_kind;
    SlLayout layout;
    void* object_data;
    size_t object_size;
    uint64_t expected_generation;
    void* context;
    SlGeneration generation;
    SlSkinAllowed skin_allowed;
    SlRefresh refresh;
    SlLog log;
} SlConfig;

enum { SL_STATE_ACTIVE = 1, SL_STATE_RECOVERY_REQUIRED = 2 };
typedef struct SlState {
    uint32_t struct_size;
    uint32_t flags;
    int32_t current_skin;
    uint32_t current_encoded_skin;
    uint32_t current_stack_skin;
    int32_t original_skin;
    uint64_t generation;
} SlState;

typedef struct SlRuntime SlRuntime;
typedef struct SlApi {
    uint32_t struct_size;
    uint32_t abi_version;
    const char* (SL_CALL *status_text)(SlStatus status);
    uint32_t (SL_CALL *encode)(uint32_t skin, uint32_t a, uint32_t key, uint32_t b);
    uint32_t (SL_CALL *decode)(uint32_t encoded, uint32_t a, uint32_t key, uint32_t b);
    SlStatus (SL_CALL *create)(const SlConfig* config, SlRuntime** output);
    SlStatus (SL_CALL *state)(SlRuntime* runtime, SlState* output);
    SlStatus (SL_CALL *apply)(SlRuntime* runtime, int32_t skin_id);
    SlStatus (SL_CALL *restore)(SlRuntime* runtime);
    /* Failed restoration retains the runtime, so recovery can be retried. */
    SlStatus (SL_CALL *destroy)(SlRuntime** runtime);
    /* Explicit disposal after the target is gone; performs no target writes. */
    SlStatus (SL_CALL *abandon_destroyed_target)(SlRuntime** runtime);
} SlApi;

typedef SlStatus (SL_CALL *SlGetApi)(uint32_t requested_abi, SlApi* output);

#ifdef __cplusplus
}
#endif
#endif
