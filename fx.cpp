#include "core.h"
#include <opxy/q31.hpp>

namespace grainfield {
namespace {

using opxy::i32;
using opxy::u32;
using opxy::q31::multiply;

const i32 one = 0x7fffffff;
const i32 wet_level = 0x7fffffff;
const i32 one_third = 0x2aaaaaab;

// Reverb: four combs and two allpasses, kept in the last words of the shared buffer.
const u32 comb_length[4] = {1031u, 1327u, 1559u, 1823u};
const u32 comb_offset[4] = {0u, 1031u, 2358u, 3917u};
const u32 allpass_length[2] = {241u, 337u};
const u32 allpass_offset[2] = {5740u, 5981u};
const u32 reverb_words = 6400u;
// SHIMMER (REVERB above 50%): an octave-up pitch shifter on the reverb tail,
// fed back into the reverb, in its own 2048-word ring before the reverb.
const u32 shimmer_words = 2048u;
const u32 shimmer_mask = shimmer_words - 1u;
const i32 comb_feedback = 0x75c28f5c;  // about 0.92: a longer tail
const i32 reverb_send = 0x4ccccccd;    // about 0.6
const i32 feedback_ceiling = 0x79999999;  // about 0.95

inline i32 nonnegative(i32 value)
{
    return value < 0 ? 0 : value;
}

inline i32 sat_add(i32 a, i32 b)
{
    const i32 result =
        static_cast<i32>(
            static_cast<u32>(a) +
            static_cast<u32>(b));

    if (((a ^ result) & (b ^ result)) < 0)
        return a < 0 ? -one : one;

    return result;
}

inline u32 grain_length_for(i32 size)
{
    const u32 position =
        static_cast<u32>(
            nonnegative(size)) >> 16;

    return 1024u +
        position * 15360u / 32767u;
}

inline i32 smoothstep(i32 x)
{
    if (x <= 0)
        return 0;

    if (x >= one)
        return one;

    const i32 x2 = multiply(x, x);
    const i32 x3 = multiply(x2, x);

    return x2 + ((x2 - x3) << 1);
}

inline i32 grain_envelope(
    u32 age,
    u32 length,
    u32 attack_length)
{
    if (!length || age >= length)
        return 0;

    const u32 attack =
        attack_length < length
            ? attack_length
            : length - 1u;
    const u32 release = length - attack;

    if (age < attack) {
        const u32 scale =
            attack
                ? 0x7fffffffu / attack
                : 0;

        const u32 phase = age * scale;

        return smoothstep(
            static_cast<i32>(
                phase > 0x7fffffffu
                    ? 0x7fffffffu
                    : phase));
    }

    const u32 remaining =
        length - 1u - age;

    const u32 scale =
        release
            ? 0x7fffffffu / release
            : 0;

    const u32 phase =
        remaining * scale;

    return smoothstep(
        static_cast<i32>(
            phase > 0x7fffffffu
                ? 0x7fffffffu
                : phase));
}

inline u32 density_period(i32 density)
{
    const u32 d =
        static_cast<u32>(
            nonnegative(density));

    const u32 lower_edge =
        0x3d70a3d7u; // approximately 48%

    if (d >= lower_edge)
        return 0;

    const u32 position = d >> 16;
    const u32 edge_position =
        lower_edge >> 16;

    // Fully left: about 512 samples between triggers.
    // Near the center dead zone: about half a second.
    return 512u +
        position * 21538u / edge_position;
}

inline u32 random_u32(State& state)
{
    u32 x = state.random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state.random_state = x;
    return x;
}

inline u32 randomized_period(
    State& state,
    i32 density)
{
    const u32 d =
        static_cast<u32>(
            nonnegative(density));

    const u32 upper_edge =
        0x428f5c29u; // approximately 52%

    if (d <= upper_edge)
        return 0;

    const u32 position =
        (d - upper_edge) >> 16;

    const u32 maximum_position =
        (0x7fffffffu - upper_edge) >> 16;

    // Near center: sparse randomized grains.
    // Fully right: dense randomized grains.
    const u32 base =
        512u +
        (maximum_position - position) *
        21538u / maximum_position;

    // Vary each interval from about half to
    // one and a half times the base period.
    const u32 spread =
        random_u32(state) % (base + 1u);

    return (base >> 1) + spread;
}

inline void start_grain(
    State& state,
    opxy::Buffer buffer,
    u32 lag,
    u32 length,
    u32 attack)
{
    for (u32 attempt = 0;
         attempt < 3;
         ++attempt) {
        const u32 slot =
            (state.next_slot + attempt) % 3u;

        Grain& grain =
            state.grains[slot];

        if (!grain.active) {
            grain.active = 1;
            grain.age = 0;
            grain.length = length;
            grain.attack = attack;

            grain.read =
                state.write >= lag
                    ? state.write - lag
                    : buffer.count +
                      state.write - lag;

            state.next_slot =
                (slot + 1u) % 3u;

            ++state.grains_started;
            return;
        }
    }

    // All three grain slots are active.
    // Skip the trigger rather than stealing a grain.
}

} // namespace

void initialize(State& state)
{
    state.write = 0;
    state.written = 0;
    state.trigger_countdown = 0;
    state.next_slot = 0;
    state.grains_started = 0;
    state.max_active = 0;
    state.random_state = 0x6d2b79f5u;
    state.reverb_ready = 0;
    state.feedback_lp = 0;
    state.freeze_seen = 0;
    state.freeze_last = 0;
    state.freeze_armed = 0;
    state.wet_gain = 0x7fffffff;
    state.scan_phase = 0;
    state.shimmer_on = 0;
    state.shimmer_write = 0;
    state.shimmer_delay = shimmer_words;
    state.shimmer_lp = 0;
    state.shimmer_in = 0;

    for (u32 i = 0; i < 6; ++i)
        state.reverb_pos[i] = 0;

    for (u32 i = 0; i < 4; ++i)
        state.reverb_lp[i] = 0;

    for (u32 i = 0; i < 3; ++i) {
        state.grains[i].active = 0;
        state.grains[i].age = 0;
        state.grains[i].length = 0;
        state.grains[i].read = 0;
        state.grains[i].attack = 0;
    }
}

void process(State& state,
             opxy::Buffer full_buffer,
             const opxy::Transport&,
             opxy::Channel left,
             opxy::Channel right,
             opxy::Parameters parameters)
{
    if (!full_buffer.words || !full_buffer.count) {
        for (unsigned i = 0;
             i < opxy::block_frames;
             ++i) {
            left[i] = 0;
            right[i] = 0;
        }
        return;
    }

    // The last part of the buffer holds the reverb; grains use the rest.
    const bool has_reverb =
        full_buffer.count > reverb_words + shimmer_words + 4096u;

    const u32 grain_words =
        has_reverb
            ? full_buffer.count - reverb_words - shimmer_words
            : full_buffer.count;

    const opxy::Buffer buffer = {
        full_buffer.words,
        grain_words
    };

    i32* const shimmer =
        full_buffer.words + grain_words;
    i32* const reverb =
        shimmer + shimmer_words;

    if (has_reverb && !state.reverb_ready) {
        for (u32 i = 0; i < shimmer_words + reverb_words; ++i)
            shimmer[i] = 0;  // the shimmer ring and the reverb, which follows it

        state.reverb_ready = 1;
    }

    const i32 time =
        nonnegative(parameters[0]);

    const i32 density =
        nonnegative(parameters[1]);

    const i32 size =
        nonnegative(parameters[3]);

    const i32 freeze =
        nonnegative(parameters[5]);

    // DRY is a wet/dry mix: as it brings the source up, the grains (frozen or
    // not, with their reverb) come down, so at full DRY only the source is heard.
    const i32 wet_target = 0x7fffffff - nonnegative(parameters[2]);

    // A restored Freeze (saved project, power-up) stays inactive until it is
    // switched in this session: only a change from Off to On freezes.
    const u32 freeze_on = freeze >= 0x01000000 ? 1u : 0u;  // 0 off; the first notch up freezes
    if (!state.freeze_seen) {
        state.freeze_seen = 1;
        state.freeze_last = freeze_on;
        state.freeze_armed = 0;
    } else if (freeze_on != state.freeze_last) {
        state.freeze_last = freeze_on;
        state.freeze_armed = freeze_on;
    }
    const bool frozen = freeze_on && state.freeze_armed;

    const i32 shape =
        nonnegative(parameters[4]);

    const i32 feedback =
        nonnegative(parameters[6]);

    const i32 reverb_amount =
        nonnegative(parameters[7]);

    // SHIMMER grows from REVERB 50% to 100%; below that it is off.
    const i32 shimmer_amount =
        reverb_amount > 0x40000000 ? (reverb_amount - 0x40000000) << 1 : 0;
    // The octave-up layer is heard directly, and a little of it goes back into
    // the reverb so it climbs further; more than that and the loop runs away.
    const i32 shimmer_feed = multiply(shimmer_amount, 0x07ae147b);  // up to 0.06 back into the reverb
    if (has_reverb && shimmer_amount && !state.shimmer_on) {
        for (u32 k = 0; k < shimmer_words; ++k) shimmer[k] = 0;   // start clean each time it engages
        state.shimmer_delay = shimmer_words;
        state.shimmer_lp = 0;
    }
    state.shimmer_on = shimmer_amount ? 1u : 0u;

    // FEEDBACK returns the wet sound into the recording, up to about 85%.
    // The curve f * (2 - f) makes the lower half of the knob audible too.
    const i32 feedback_curve =
        sat_add(feedback, multiply(feedback, one - feedback));
    const i32 feedback_gain =
        multiply(feedback_curve, feedback_ceiling);

    const u32 regular_period =
        density_period(density);

    const bool random_side =
        static_cast<u32>(density) >
        0x428f5c29u;
    const bool density_active =
        regular_period || random_side;

    const u32 next_length =
        grain_length_for(size);

    // SHAPE sets how much of each grain is fade-in, from 5% to 95%.
    const u32 shape_position =
        static_cast<u32>(shape) >> 16;

    const u32 attack_fraction =
        1638u + (shape_position * 29491u + 16384u) / 32768u;

    u32 next_attack =
        (next_length * attack_fraction) >> 15;

    if (next_attack < 1)
        next_attack = 1;

    // TIME scans from recent audio to nearly the oldest usable
    // material in the larger recording buffer.
    const u32 minimum_lag = 512;
    const u32 safety = 2048;

    const u32 maximum_lag =
        buffer.count > minimum_lag + safety
            ? buffer.count - safety
            : minimum_lag;

    const u32 range =
        maximum_lag - minimum_lag;

    const u32 position =
        static_cast<u32>(time) >> 16;

    u32 lag =
        minimum_lag +
        (position * range >> 15);

    // While frozen, FEEDBACK has nothing to feed back, so it scans instead: a
    // smooth back-and-forth LFO moves the TIME position through the frozen
    // sound around where TIME is set. Turning it up sweeps wider and faster,
    // from a narrow drift every 20 s to the whole sound every 2 s.
    if (frozen && feedback > 0) {
        const u32 amount = static_cast<u32>(feedback) >> 16;           // 0..32767
        state.scan_phase += 311000u + amount * 85u;                     // 0.05 .. 0.5 cycles per second
        const i32 x = static_cast<i32>(state.scan_phase) >> 16;         // -32768..32767 = one cycle
        const i32 ax = x < 0 ? -x : x;
        const i32 wave = (x * (32768 - ax)) >> 13;                      // smooth sine shape, +-32768
        const u32 depth = 1638u + (amount * 31129u >> 15);              // 5% .. 100% of the half-range
        const i32 half = static_cast<i32>(range >> 1);
        const i32 offset = ((wave * static_cast<i32>(depth)) >> 15) * (half >> 4) >> 11;
        i32 moved = static_cast<i32>(lag) + offset;
        if (moved < static_cast<i32>(minimum_lag)) moved = static_cast<i32>(minimum_lag);
        if (moved > static_cast<i32>(maximum_lag)) moved = static_cast<i32>(maximum_lag);
        lag = static_cast<u32>(moved);
    } else if (!frozen) {
        state.scan_phase = 0;  // each freeze starts its scan from the TIME position
    }

    for (unsigned i = 0;
         i < opxy::block_frames;
         ++i) {
        const i32 input =
            (left[i] >> 1) +
            (right[i] >> 1);

        // FREEZE preserves the captured buffer by stopping recording.
        if (!frozen) {
            buffer.words[state.write] =
                sat_add(
                    input,
                    multiply(
                        feedback_gain,
                        state.feedback_lp));
        }

        if (density_active && state.written > lag) {
            if (!state.trigger_countdown) {
                // While frozen the recording stops, so a grain that reads past
                // the frozen write point would cross from the newest audio to
                // the oldest and click. Start it far enough back to finish first.
                u32 grain_lag = lag;
                if (frozen && grain_lag < next_length + 64u)
                    grain_lag = next_length + 64u;
                if (grain_lag > maximum_lag)
                    grain_lag = maximum_lag;

                start_grain(
                    state,
                    buffer,
                    grain_lag,
                    next_length,
                    next_attack);

                state.trigger_countdown = random_side ? randomized_period(state, density) : regular_period;
            } else {
                --state.trigger_countdown;
            }
        } else {
            state.trigger_countdown = 0;
        }

        i32 grain_output[3] = {0, 0, 0};
        u32 active_count = 0;

        for (u32 g = 0; g < 3; ++g) {
            Grain& grain = state.grains[g];

            if (!grain.active)
                continue;

            ++active_count;

            const i32 envelope =
                grain_envelope(
                    grain.age,
                    grain.length,
                    grain.attack);

            grain_output[g] =
                multiply(
                    buffer.words[grain.read],
                    envelope);

            if (++grain.read >= buffer.count)
                grain.read = 0;

            if (++grain.age >= grain.length)
                grain.active = 0;
        }

        if (active_count > state.max_active)
            state.max_active = active_count;

        // Every grain keeps the same gain for its complete lifetime.
        // Starting or ending another grain no longer changes the level
        // of grains that are already playing.
        i32 wet = sat_add(
            multiply(
                grain_output[0],
                one_third),
            multiply(
                grain_output[1],
                one_third));

        wet = sat_add(
            wet,
            multiply(
                grain_output[2],
                one_third));

        // 2x makeup gain (saturating) so the wet signal is not so quiet.
        wet = sat_add(wet, wet);

        // Feedback tap: the playing grains' average (not the 1/3-scaled mix), gently
        // low-passed, so FEEDBACK clearly recycles the sound.
        // Divided by the number of playing grains so the loop cannot run away.
        i32 grain_sum =
            sat_add(sat_add(grain_output[0], grain_output[1]), grain_output[2]);
        if (active_count == 2)
            grain_sum >>= 1;
        else if (active_count == 3)
            grain_sum = multiply(grain_sum, one_third);
        // Lift the average so FEEDBACK is clearly audible, while staying
        // below the level where the loop runs away.
        grain_sum = sat_add(grain_sum, sat_add(grain_sum >> 1, grain_sum >> 2));
        state.feedback_lp +=
            (grain_sum >> 2) - (state.feedback_lp >> 2);

        i32 out_left = wet;
        i32 out_right = wet;

        if (has_reverb) {
            i32 send =
                multiply(wet, reverb_send);

            if (shimmer_amount) {
                // Two read taps run through the ring at twice the write speed
                // (an octave up), half the ring apart, crossfading so each
                // tap is silent at its wrap.
                const u32 w = state.shimmer_write;
                shimmer[w] = state.shimmer_in;
                const u32 d = state.shimmer_delay;                       // 1..2048
                const i32 a = shimmer[(w - d) & shimmer_mask];
                const i32 b = shimmer[(w - d - (shimmer_words >> 1)) & shimmer_mask];
                const i32 wa = static_cast<i32>(d < 1024u ? d : 2048u - d);  // 0..1024
                const i32 shifted = ((a >> 11) * wa + (b >> 11) * (1024 - wa)) << 1;
                state.shimmer_delay = d > 1u ? d - 1u : shimmer_words;
                state.shimmer_write = (w + 1u) & shimmer_mask;
                state.shimmer_lp += (shifted >> 2) - (state.shimmer_lp >> 2);  // soften the top
                send = sat_add(send, multiply(state.shimmer_lp, shimmer_feed));
            }

            i32 sums[2] = {0, 0};

            for (u32 c = 0; c < 4; ++c) {
                i32* line = reverb + comb_offset[c];
                u32& pos = state.reverb_pos[c];
                const i32 delayed = line[pos];

                state.reverb_lp[c] +=
                    (delayed >> 1) -
                    (state.reverb_lp[c] >> 1);

                line[pos] =
                    sat_add(
                        send,
                        multiply(
                            state.reverb_lp[c],
                            comb_feedback));

                if (++pos >= comb_length[c])
                    pos = 0;

                sums[c & 1] =
                    sat_add(
                        sums[c & 1],
                        delayed >> 1);
            }

            i32 tail[2] = {0, 0};

            for (u32 a = 0; a < 2; ++a) {
                i32* line = reverb + allpass_offset[a];
                u32& pos = state.reverb_pos[4 + a];
                const i32 delayed = line[pos];

                const i32 mixed =
                    sat_add(sums[a], delayed >> 1);

                line[pos] = mixed;

                tail[a] =
                    sat_add(delayed, -(mixed >> 1));

                if (++pos >= allpass_length[a])
                    pos = 0;
            }

            state.shimmer_in = (tail[0] >> 1) + (tail[1] >> 1);

            // The shimmer layer, heard beside the reverb tail.
            i32 shimmer_out = shimmer_amount ? multiply(state.shimmer_lp, shimmer_amount) : 0;
            shimmer_out = sat_add(shimmer_out, shimmer_out >> 1);  // 1.5x: clearly there at full REVERB
            out_left = sat_add(out_left, shimmer_out);
            out_right = sat_add(out_right, shimmer_out);

            // The tail is doubled so REVERB is clearly audible.
            const i32 lifted_left = multiply(tail[0], reverb_amount);
            out_left =
                sat_add(
                    out_left,
                    sat_add(lifted_left, lifted_left));

            // The tail is doubled so REVERB is clearly audible.
            const i32 lifted_right = multiply(tail[1], reverb_amount);
            out_right =
                sat_add(
                    out_right,
                    sat_add(lifted_right, lifted_right));
        }

        state.wet_gain += (wet_target >> 10) - (state.wet_gain >> 10);  // eased, no clicks
        left[i] = multiply(out_left, state.wet_gain);
        right[i] = multiply(out_right, state.wet_gain);

        if (!frozen) {
            if (++state.write >= buffer.count)
                state.write = 0;

            if (state.written < buffer.count)
                ++state.written;
        }
    }
}

} // namespace grainfield
