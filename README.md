# Grainfield

A granular send effect for the Teenage Engineering OP–XY, built with the
unofficial [Open-XY SDK](https://github.com/etcetc000/open-xy). It records what
you send it and plays it back as a cloud of grains, with feedback, a reverb that
turns to shimmer, and a freeze that can scan through what it holds.

## Controls

| Page 1 | Page 2 |
| --- | --- |
| **Time**: how far back in the recording grains start | **Shape**: grain fade-in vs fade-out |
| **Density**: sparse grains to a dense cloud | **Freeze**: 0 is off; any amount above 0 stops recording and holds the sound |
| **Dry**: original sound vs grains (a wet/dry mix) | **Feedback**: grains back into the recording; while frozen, a scan LFO (below) |
| **Size**: grain length | **Reverb**: reverb tail; above 50% an octave-up shimmer fades in |

- **Dry** works as a wet/dry mix: turning it up brings the original sound up
  and the grains (frozen or not, with their reverb) down, so it is also the
  volume control for a frozen sound. At 0 you hear only the grains; at 100 only
  the original sound. The effect amount is still the track's send level.
- **Freeze** is off at 0 and freezes from the first notch up. It only takes
  hold when turned up during a session: a project or power-up that restores it
  above 0 keeps recording until you turn it to 0 and back up.
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

## Changes

- **v1.2**: Freeze is off at 0 and freezes from the first notch; Dry is now a
  wet/dry mix, so it turns a frozen sound down.
- **v1.1**: first public release.

## Licence

MIT, see [LICENSE](LICENSE). By Colors In Space.
