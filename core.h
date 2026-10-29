#pragma once
#include <opxy/effect.hpp>

namespace grainfield {

struct Grain {
    opxy::u32 active;
    opxy::u32 age;
    opxy::u32 length;
    opxy::u32 read;
    opxy::u32 attack;
};

struct State {
    opxy::u32 write;
    opxy::u32 written;
    opxy::u32 trigger_countdown;
    opxy::u32 next_slot;
    opxy::u32 grains_started;
    opxy::u32 max_active;
    opxy::u32 random_state;
    Grain grains[3];
    opxy::u32 reverb_ready;
    opxy::u32 reverb_pos[6];
    opxy::i32 reverb_lp[4];
    opxy::i32 feedback_lp;
    // FREEZE follows switches made since this instance started: a project or
    // power-up that restores it On does not freeze until it is switched again.
    opxy::u32 freeze_seen;
    opxy::u32 freeze_last;
    opxy::u32 freeze_armed;
    opxy::u32 scan_phase;  // FEEDBACK's scan LFO while frozen
    // SHIMMER's octave-up shifter on the reverb tail
    opxy::u32 shimmer_on, shimmer_write, shimmer_delay;
    opxy::i32 shimmer_lp, shimmer_in;
};

void initialize(State& state);

void process(State& state,
             opxy::Buffer buffer,
             const opxy::Transport& transport,
             opxy::Channel left,
             opxy::Channel right,
             opxy::Parameters parameters);

}
