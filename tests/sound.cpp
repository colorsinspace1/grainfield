#include "../fx.cpp"
#include <cstdio>

static void fill_input(
    opxy::i32* left,
    opxy::i32* right,
    opxy::i32 value = 0x10000000)
{
    for (unsigned i = 0;
         i < opxy::block_frames;
         ++i) {
        left[i] = value;
        right[i] = value;
    }
}

static bool channels_match(
    const opxy::i32* left,
    const opxy::i32* right)
{
    for (unsigned i = 0;
         i < opxy::block_frames;
         ++i) {
        if (left[i] != right[i])
            return false;
    }

    return true;
}

int main()
{
    opxy::i32 memory[65536] = {};

    opxy::Buffer buffer = {
        memory,
        65536
    };

    opxy::Transport transport = {};

    opxy::i32 left[opxy::block_frames];
    opxy::i32 right[opxy::block_frames];

    // Eight parameters:
    // Time, Density, Dry, Size,
    // Shape, Freeze, Feedback, Reverb.
    opxy::i32 parameters[8] = {
        0,
        0x40000000,
        0,
        0x60000000,
        0x53333333,
        0,
        0,
        0
    };

    // Density center: no automatic wet grains.
    {
        grainfield::State state = {};
        grainfield::initialize(state);

        for (unsigned block = 0;
             block < 320;
             ++block) {
            fill_input(left, right);

            grainfield::process(
                state,
                buffer,
                transport,
                left,
                right,
                parameters);

            for (unsigned i = 0;
                 i < opxy::block_frames;
                 ++i) {
                if (left[i] || right[i]) {
                    std::puts(
                        "FAIL center Density produced wet audio");
                    return 1;
                }
            }
        }

        if (state.grains_started) {
            std::puts(
                "FAIL center Density started grains");
            return 1;
        }
    }

    // Regular Density below center.
    {
        for (unsigned i = 0; i < 65536; ++i)
            memory[i] = 0;

        grainfield::State state = {};
        grainfield::initialize(state);

        parameters[1] = 0;
        parameters[5] = 0;

        bool heard_wet = false;
        opxy::i32 previous_last = 0;

        for (unsigned block = 0;
             block < 600;
             ++block) {
            fill_input(left, right);

            grainfield::process(
                state,
                buffer,
                transport,
                left,
                right,
                parameters);

            if (!channels_match(left, right)) {
                std::puts(
                    "FAIL regular Density channels differ");
                return 1;
            }

            for (unsigned i = 0;
                 i < opxy::block_frames;
                 ++i) {
                if (left[i])
                    heard_wet = true;
            }

            const opxy::i32 difference =
                left[0] - previous_last;

            if (difference > 0x02000000 ||
                difference < -0x02000000) {
                std::puts(
                    "FAIL regular Density discontinuity");
                return 1;
            }

            previous_last =
                left[opxy::block_frames - 1];
        }

        if (!heard_wet ||
            state.grains_started < 3 ||
            state.max_active < 3) {
            std::puts(
                "FAIL regular Density did not overlap");
            return 1;
        }
    }

    // Randomized Density above center.
    {
        for (unsigned i = 0; i < 65536; ++i)
            memory[i] = 0;

        grainfield::State state = {};
        grainfield::initialize(state);

        const opxy::u32 initial_random =
            state.random_state;

        parameters[1] = 0x68000000;
        parameters[5] = 0;

        bool heard_wet = false;

        for (unsigned block = 0;
             block < 700;
             ++block) {
            fill_input(left, right);

            grainfield::process(
                state,
                buffer,
                transport,
                left,
                right,
                parameters);

            if (!channels_match(left, right)) {
                std::puts(
                    "FAIL random Density channels differ");
                return 1;
            }

            for (unsigned i = 0;
                 i < opxy::block_frames;
                 ++i) {
                if (left[i])
                    heard_wet = true;
            }
        }

        if (!heard_wet ||
            state.grains_started < 3) {
            std::puts(
                "FAIL randomized Density produced no grains");
            return 1;
        }

        if (state.random_state == initial_random) {
            std::puts(
                "FAIL randomized Density did not advance");
            return 1;
        }
    }

    // FREEZE must stop both the recording head
    // and changes to captured buffer contents.
    {
        for (unsigned i = 0; i < 65536; ++i)
            memory[i] = 0;

        grainfield::State state = {};
        grainfield::initialize(state);

        parameters[1] = 0;
        parameters[5] = 0;

        // Record live audio before freezing.
        for (unsigned block = 0;
             block < 400;
             ++block) {
            fill_input(
                left,
                right,
                0x10000000);

            grainfield::process(
                state,
                buffer,
                transport,
                left,
                right,
                parameters);
        }

        const opxy::u32 frozen_write =
            state.write;

        const opxy::u32 frozen_written =
            state.written;

        opxy::i32 frozen_snapshot[64];

        for (unsigned i = 0; i < 64; ++i)
            frozen_snapshot[i] = memory[i];

        // Turn FREEZE on and send different input.
        parameters[5] = 0x7fffffff;

        for (unsigned block = 0;
             block < 200;
             ++block) {
            fill_input(
                left,
                right,
                0x30000000);

            grainfield::process(
                state,
                buffer,
                transport,
                left,
                right,
                parameters);
        }

        if (state.write != frozen_write ||
            state.written != frozen_written) {
            std::puts(
                "FAIL Freeze moved the recording head");
            return 1;
        }

        for (unsigned i = 0; i < 64; ++i) {
            if (memory[i] != frozen_snapshot[i]) {
                std::puts(
                    "FAIL Freeze changed captured audio");
                return 1;
            }
        }
    }

    return 0;
}
