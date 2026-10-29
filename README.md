# Grainfield

A granular send effect for the Teenage Engineering OP–XY, built with the
unofficial [Open-XY SDK](https://github.com/etcetc000/open-xy). It records what
you send it and plays it back as a cloud of grains, with feedback, a reverb that
turns to shimmer, and a freeze that can scan through what it holds.

## Controls

| Page 1 | Page 2 |
| --- | --- |
| **Time**: how far back in the recording grains start | **Shape**: grain fade-in vs fade-out |
| **Density**: sparse grains to a dense cloud | **Freeze**: Off / On: stop recording and hold the sound |
| **Dry**: how much of the original sound passes through | **Feedback**: grains back into the recording; while frozen, a scan LFO (below) |
| **Size**: grain length | **Reverb**: reverb tail; above 50% an octave-up shimmer fades in |

- **Dry** is the OP–XY's source dry level, not a mix: at 0 you hear only the
  effect, at 100 the original sound passes through in full with the effect on
  top. The effect amount is the track's send level.
- **Freeze** only takes hold when switched on during a session: a project or
  power-up that restores it On keeps recording until you switch it Off and On.
- **Feedback while frozen** becomes a smooth back-and-forth scan through the
  frozen sound, centred on Time: from a narrow drift every 20 s to the whole
  sound every 2 s.

## Page 2 needs an SDK patch

The OP–XY's send page shows four knobs. **Page 2 (hold encoder 1 and turn)**
and the correct **Dry on send bus 2** come from changes to the Open-XY SDK
itself, published separately as `open-xy-send-effect-page2.patch`. With the
unpatched SDK, Grainfield builds and plays, but only Page 1's knobs are
reachable by hand and Dry is reversed on bus 2.

## Build

With the Open-XY SDK 0.0.2 (plus the patch for Page 2), on OP–XY OS 1.2.1:

```sh
python sdk.py test --machine path/to/grainfield
python sdk.py kit --personal --stock-os <your stock os.ldr> --collection <catalog collection> --machine path/to/grainfield --out <folder>
```

An effects-only build needs at least one synth beside it (the SDK's
`catalog fetch` supplies the published ones). Firmware is always built from
your own OP–XY's OS; no OS is distributed here.

## Notes

- Uses a 256 KiB send-slot buffer (about 1.3 s of recording).
- CPU use is close to the SDK's 30,000-instruction aim at its busiest
  settings (high Density and Reverb); not yet measured on the device.
- Unofficial and experimental. Not affiliated with or endorsed by Teenage
  Engineering.

## Licence

MIT, see [LICENSE](LICENSE). By Colors In Space.
