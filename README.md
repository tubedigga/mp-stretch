# MP Stretch

A stereo insert for Akai MPC standalone, by Tubedigga. The screen name is MP Stretch. The uid is still `Pi2G`.

The grains run on the whole track. There is one page, STRETCH, with six controls. The MPC sequencer automates them directly. Neutral, and the defaults, are Cyclic 400 samples, Length 100%, Pitch off, Phase off, Delay 0, and Rate 44100 Hz. At neutral the live input is passed through. Delay at 0 leaves that path exactly as it is, including the snap back onto the live chop. Rate at 44100 Hz is full band. Below that the internal clock runs at the chosen rate and the top cuts at half of it, down to 22500 Hz.

| Control | Range | Neutral |
| --- | --- | --- |
| Cyclic | 0–2000 samples | 400 |
| Length | 100–2000% | 100% |
| Pitch | Off / On | Off |
| Phase | 0–30 ms | off |
| Delay | 0–2000 ms | 0 |
| Rate | 22500–44100 Hz | 44100 |

Pitch off keeps the original pitch and lets Length change the duration. Pitch on keeps the original duration and uses Length as the pitch, so 200% is one octave up. Phase is a 0–30 ms delay with 50% feedback on the dry signal while Length and Cyclic are home, and on the grains when they are not. Moving a control is heard on the next block.

Q-Link 13 is Cyclic, 14 Length, 15 Delay, and 16 Phase. Pitch and Rate are not Q-Links. Double-tap a knob to type a value. Above 0, and only while Length is away from 100%, Delay waits for a new hit, waits out the time, then stretches the captured tail. The stretch position moves at 100/Length. The samples keep their pitch unless Pitch is on. One second of tail at 2000% is twenty seconds of output, cut off at 16. It fades when the stretch position reaches the end of the tail, or at 16 seconds. Phase at 0 is dry. Moving Phase glides the delay time.

## Licence

This project is GPL-3.0. The grain stretch is modified TimeStretch.h from https://github.com/dar-io-p/potenza-time-stretch (GPL-3.0, dar-io-p).

The Akai MPC Plugin Builder SDK is MIT, Copyright (c) 2026 inMusic Brands, Inc., https://github.com/inmusic-software/mpc-plugin-builder-sdk.

## Build

From the SDK:

```
python3 tools/build.py "<this folder>"
```

The folder to copy onto the MPC is `Tubedigga - VST - MP Stretch`, into `VST2 Plugins`. Do not copy the `build/` directory that is already in this source tree.

MPC OS 3.10.1 or later. Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin Support.
