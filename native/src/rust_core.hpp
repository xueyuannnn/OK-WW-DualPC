#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>

// Typed ABI only. Qt objects and C++ exceptions never cross this boundary.
namespace okww::rust {
enum class RotationOp : int32_t;
enum class RotationMethod : int32_t;
struct Freeze {
    double start, duration, threshold;
};
struct Timeline;
struct Gate;
struct Character {
    int32_t index, role;
    double buffTime, lastBuff, lastSwitch, lastSwitchIn, lastFullConSwitch, currentCon;
    uint32_t flags, reserved;
};
struct Candidate {
    Character character;
    int32_t priority, valid;
};
struct Decision {
    int32_t index, reason, priority, reserved;
};
struct Spend {
    int32_t used, repeat;
};
struct Card {
    int32_t rarity, recommended, preferred, x;
};
struct Item {
    int32_t index, price, hasPrice, recommended;
};
struct Restart {
    int32_t actions[4]{}, requested = 0, loading = 0, pending = 0;
};
struct ExecutionHost {
    void *context;
    int32_t (*now)(void *, double *);
    int32_t (*sleep)(void *, double);
    int32_t (*checkpoint)(void *);
    int32_t (*condition)(void *, int32_t *);
};
struct TransportHost {
    void *context;
    int64_t (*now)(void *);
    int32_t (*send)(void *, const uint8_t *, size_t);
    int32_t (*readable)(void *, int64_t remaining);
    int32_t (*receive)(void *, uint8_t *, size_t);
};
struct RotationHost {
    void *context;
    int32_t (*invoke)(void *, int32_t, const double *, size_t, double *out3);
    int32_t (*query)(void *, int32_t, const uint8_t *, size_t, const uint8_t *, size_t, const double *,
                     size_t, double *out3) = nullptr;
};
struct SpectrumHost {
    void *context;
    int32_t (*transform)(void *, const double *, size_t, double *);
};
static_assert(sizeof(RotationHost) == 24);
static_assert(sizeof(Freeze) == 24 && sizeof(Character) == 64 && sizeof(Candidate) == 72);
static_assert(sizeof(Decision) == 16 && sizeof(Restart) == 28);
extern "C" {
uint32_t okww_rs_abi_version();
int32_t okww_rs_gauge_segments(const SpectrumHost *, const uint8_t *, size_t width, size_t height,
                               size_t stride, int32_t kind, int32_t segments, int32_t minFrequency,
                               int32_t maxFrequency, double minAmplitude, int32_t *out2);
int32_t okww_rs_spectrum_magnitudes(const double *complex, size_t count, double *out, size_t capacity);
double okww_rs_gauge_ratio(uint64_t colored, uint64_t total);
double okww_rs_stripe_ratio(int32_t start, int32_t end, int32_t width, uint64_t colored, uint64_t total);
double okww_rs_gauge_round(double value, int32_t decimals);
int32_t okww_rs_ring_bounds(int32_t height, double inner, double outer, int32_t nonzero, int32_t *out2);
int32_t okww_rs_rotation_run(const RotationHost *, int32_t method, const double *, size_t, double *out3);
int32_t okww_rs_toolbox_intro(int32_t intro, int32_t previous);
int32_t okww_rs_transport_exchange(const TransportHost *, const uint8_t *, size_t, int64_t timeout,
                                   uint32_t seq, uint32_t command);
int32_t okww_rs_latency_percentiles(double *samples, size_t count, double *out2);
int32_t okww_rs_checkpoint(const Gate *, uint64_t, void *, int32_t (*health)(void *));
int32_t okww_rs_execution_wait(const ExecutionHost *, double seconds);
int32_t okww_rs_execution_until(const ExecutionHost *, double timeout, double interval);
int32_t okww_rs_frame_fresh(int64_t now, int64_t captured, int32_t present, double staleAfter);
int32_t okww_rs_watchdog(int64_t now, int32_t armed, int32_t boxConnected, int64_t heartbeat);
int32_t okww_rs_capture_recovery(int64_t now, int32_t present, int32_t stopped, int64_t *began);
Timeline *okww_rs_freeze_new();
void okww_rs_freeze_free(Timeline *);
Timeline *okww_rs_freeze_clone(const Timeline *);
int32_t okww_rs_freeze_add(Timeline *, double now, double start, double duration, double threshold);
double okww_rs_freeze_elapsed(const Timeline *, double now, double start, int32_t intro);
void okww_rs_freeze_clear(Timeline *);
size_t okww_rs_freeze_count(const Timeline *);
int32_t okww_rs_freeze_entries(const Timeline *, Freeze *, size_t capacity);
double okww_rs_character_status(const Timeline *, const Character *, double now, int32_t kind);
void okww_rs_switch_out(Character *, int32_t full, double now);
void okww_rs_apply_intro(int32_t previousRole, Character *, int32_t intro);
int32_t okww_rs_choose_switch(const Character *, const Candidate *, size_t count, int32_t intro,
                              const Timeline *, double now, Decision *);
int32_t okww_rs_plan_stamina(int32_t current, int32_t reserve, int32_t total, int32_t once, int32_t remaining,
                             int32_t exact, int32_t twice, Spend *);
int32_t okww_rs_daily_batch(int32_t progress, int32_t cost, int32_t available, int32_t twice, int32_t *out);
int32_t okww_rs_select_card(const Card *, size_t count);
int32_t okww_rs_order_items(const Item *, size_t count, int32_t money, size_t *out, size_t capacity);
int32_t okww_rs_restart_step(Restart *, int32_t kind, int32_t hud, int32_t realm);
Gate *okww_rs_gate_new();
void okww_rs_gate_free(Gate *);
uint64_t okww_rs_gate_start(const Gate *);
void okww_rs_gate_stop(const Gate *);
int32_t okww_rs_gate_active(const Gate *, uint64_t epoch);
int32_t okww_rs_pixel_round(double value, int32_t *out);
int32_t okww_rs_echo_simplify(char16_t *words, size_t length);
int32_t okww_rs_garden_pixels(const uint8_t *data, int32_t width, int32_t height, size_t stride, int32_t kind);
int32_t okww_rs_key_code(const uint8_t *, size_t length);
void okww_rs_packet_header(uint32_t uuid, uint32_t random, uint32_t seq, uint32_t command, uint8_t *out16);
int32_t okww_rs_matching_reply(const uint8_t *, size_t length, uint32_t seq, uint32_t command);
void okww_rs_mouse_report(int32_t buttons, int32_t x, int32_t y, int32_t wheel, uint8_t *out56);
int32_t okww_rs_keyboard_report(int32_t modifiers, const int32_t *, size_t count, uint8_t *out12);
}
inline void verifyAbi() {
    static const bool valid = okww_rs_abi_version() == 2;
    if (!valid)
        throw std::runtime_error("Rust 内核接口版本不匹配");
}
} // namespace okww::rust
