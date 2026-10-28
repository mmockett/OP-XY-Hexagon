# Hollow

A resonator choir. A breath (noise) and a soft bow (a pulse train at the note's
pitch) drive eight tuned resonant modes per voice. The body they belong to
morphs from a sung vowel through a string and a stopped pipe to glass. Held
notes also ring sympathetically in one another: each voice keeps quiet modes
at the pitches of the other notes held, so a chord blooms into one sound.
Long rings turn breath into slowly drifting pure tones; short ones give
breathy, noisy formant bands.

## Controls

Page 1 holds what is played most; page 2 sits under it, one alternate per encoder.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | breath | air |
| 2 | body | vowel |
| 3 | bright | spread |
| 4 | ring | sympathy |

| Control | Range | Default | Caption | What it does |
| --- | --- | --- | --- | --- |
| breath | 0–100 | 35 | amount | Bow to breath. At 0 the bow holds steady tones (with a little bow hiss). At 100 noise alone drives the modes. The two cross at about equal power. |
| body | vox, str, pipe, glass | vox | family, or `vo/st`-style between two | Resonator family, morphing continuously from one to the next. vox: harmonics 1–7 and 9 shaped by the vowel's formants. str: harmonics 1–8, bright and slow to fade. pipe: a stopped pipe's odd harmonics. glass: inharmonic bowl partials in beating pairs. |
| bright | 0–100 | 45 | amount | The bow's harmonics fall from about 9 dB per octave (closed) to 2 dB (open). The breath's low-pass opens from 2× to 128× the note. Loudness stays roughly even. |
| ring | 0.1–20 s (before the body's scale) | 1.0 s | ring time of the fundamental, scaled by the body (vox 0.7×, pipe 0.85×, glass 1.5×), e.g. `1.0s`, `14s` | How long the modes ring: the bloom when a note starts, and how narrow and pure the breath's tones are. Higher modes fade sooner by the body's tilt. |
| air | 0–100 | 0 | amount | Unresonated breath hiss (about 450 Hz to 5 kHz) mixed in. |
| vowel | oo, oh, ah, eh, ee | ah | nearest vowel | The formant envelope over absolute frequency. It sets the vox body fully and colours the other bodies partly (str 30%, pipe 45%, glass 15%). |
| spread | 0–15 cents | 0 | cents, e.g. `7c` | Half the modes go flat by up to 15 cents, each still driven at its own pitch, as if by a second, flattened bow. The two sets of partials drift against each other. The fundamental stays on the note. |
| sympathy | 0–100 | 0 | amount | How strongly this voice rings at other held notes' pitches. The breath excites those modes. The bow excites them only where one of this note's harmonics falls within their resonance, so octaves, fifths and twelfths bloom most. |

Air, spread and sympathy colour the sound, so they default to off. Sympathy is
the machine's signature: the presets (Choir, Dusk, Bowed glass, Bottle,
Strings) turn it up.

## How it works

Each mode is a damped resonator solved in phasor form. Once per 64-sample
block, its complex amplitude decays by the mode's ring time, moves toward the
bow's steady push, and is jostled by the breath's noise. This is exactly how a
resonator's envelope evolves when it is driven near its own frequency. A sine
oscillator then plays that amplitude, ramped across the block. The per-block
decay factor is below 1 at every setting, so no mode can grow at any Ring.
The per-sample cost is one table read and two 16×16 multiplies per mode. All
exponentials, damping and gains are planned once per block.

The output gain holds the body's steady power, so bodies, vowels, Bright and
the keyboard's range all sound about equally loud (the gain stays within
−6 dB to +12 dB). A soft limit keeps every sample within half scale. The
output sits low enough that eight notes keep headroom: a full chord peaks
near 0.6 of full scale, and about 1.0 with Breath and Air both at 100.

### Sympathy and `Shared`

`Shared` holds one registry entry per sounding voice: owner, pitch increment,
and the clock value when it was last refreshed.

- `start` claims the entry. Each `render` refreshes it.
- `destroy` clears every entry with that owner.
- An entry not refreshed within 24 renders by any voice is ignored, and
  another voice can reclaim it. This covers the case where `destroy` is never
  called.
- Each voice gives the first four other sounding notes one sympathetic mode
  each. When a note goes, its mode fades out over one block and is freed.
- All of this work is bounded by the 8 entries.

The registry is shared by every voice of the machine, so Hollow notes on two
tracks also ring into each other.

## Screen

A shelf of eight vessels in the body's shape: an urn (vox), a tall string
body with its peg (str), an organ pipe with its mouth (pipe) or a wine glass
(glass), whichever family Body is nearest. Each held note lights one vessel,
left to right. A lit vessel swells as its note blooms, more slowly with more
breath and a longer ring, and glows brighter as it fills. Once released, it
rings out over Ring's time, as the engine maps it (`ring_log2_seconds`).

From each ringing mouth:

- Ripples rise and fade. They are slower and wider the longer the ring, and
  brighter with Bright. Vowel sets their shape, round for oo and flat for ee.
  Spread doubles them, offset either side.
- Breath motes drift up: none to three, by Breath.
- On glass, glints catch on the bowl: none to two, by Bright.

Sympathy arcs join vessels that ring together, within three of each other,
and pulse slowly. They brighten as both vessels ring and as Sympathy rises.
The air line along the top stirs with everything that sounds, more with Air.
The two-page header shows each body's family, the ring time as the body
scales it, the nearest vowel and the spread in cents.

The screen cannot hear the voices, so it lights vessels by the held-note
count. A note played and released between frames still lights the next
vessel briefly. When nothing rings the screen is still. Reduced motion, a
reset and a track change show the end state at once.

`tools/make_panel.py` writes `panel.svg.fragment` with the node numbers
`display.h` names. It uses 253 nodes and 2 paths of 64 points. Each vowel
has its own pool of 29 ripples, because nodes scale only uniformly. A full
chord with Spread up could show 48, so the faintest are left out.

## For the screen

`modes.h` is deterministic and has no audio state, so the display can include
it. It provides:

- `body_mode(body, k)`: ratio, log2 ratio and level for mode k
- `body_traits(body)`
- `mode_level(body, vowel, k, increment)`: the level audio uses before
  breath, bright and output gain
- `mode_increment(body, k, increment)`
- `vowel_curve(vowel, increment)`
- `ring_log2_seconds(ring, body)`
- the caption writers: `body_caption`, `ring_caption`, `vowel_caption`,
  `spread_caption`

Controls are 0..32767; use `control(q31)` to convert a Q31 value.

## CPU

The host estimate (`sdk.py test`) is about 9,000 Blackfin instructions per
voice block at the heaviest setting. Each mode's sample loop compiles to 17
Blackfin instructions in a zero-overhead hardware loop, so twelve modes (eight
body modes and four sympathetic ones) cost about 13,000 instructions a block
before the per-block planning. These are estimates, not device measurements.
