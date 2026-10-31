# Bloom

Bloom is an additive pad voice. Each note is a bank of 24 sine partials.
Page 1 shapes the spectrum: its slope, a saw-to-square-to-pulse shape,
inharmonic stretch and a sung vowel. Page 2 sets how it moves: every partial
drifts in level on its own (seeded, smoothed random walks), haze turns each
partial into a detuned pair that beats like a chorus, and blur smears each
partial into a narrow band of noise. With page 2 at zero it is a clean, still
additive tone; turned up, it gives hazy, slightly detuned, nostalgic textures,
breathing choirs, PWM strings, glassy bells and glitter.

A track's voices also share one slow "weather" gust. It leans the whole
chord brighter or darker together, so a big chord still audibly breathes
instead of averaging out to a blur. Each note's evolution is deterministic:
the same notes, played in the same order, sound the same.

## Controls

Press any encoder to switch pages.

| Page | Control | Range | Default | Caption | What it does |
| --- | --- | --- | --- | --- | --- |
| 1 | tilt | -24 to +6 dB/oct | -9 | `-9dB` | Spectral slope, from almost a pure sine to bright and glassy. Loudness stays even across the range |
| 1 | shape | saw, square, pulse 50% to 6% | saw | `saw` `sq40` `sqr` `pw18` | The first half fades the even partials, from a saw-like full spectrum to a hollow square. The second half narrows a pulse: notches every 1/width partials move down through the spectrum, ending thin and nasal. Breath makes the width wander (PWM) |
| 1 | stretch | 2.00 to 3.00 | 2.00 | `2.00` | Inharmonicity: partial *k* sits at *k*^(1+s). The caption shows partial 2's ratio. Low values shimmer like a piano, high ones ring like bells and glass |
| 1 | vowel | off, oo, oh, ah, eh, ee | off | `off` `ah` | Two formants (up to 30 dB) at a sung vowel's frequencies, fixed in Hz like a voice's, so the whole keyboard sings the same vowel. Fades in over the first tenth of the travel, then moves oo, oh, ah, eh, ee. Breath makes the vowel wander |
| 2 | breath | 0 to 100 | 0 | `0` | How far everything drifts: each partial's level up to 18 dB either way, the shared weather's lean up to 4.5 dB/oct, the pulse width and the vowel |
| 2 | blur | 0 to 120 cents | 0 | `42c` | Each partial's pitch jitters by smoothed noise (8 to 38 Hz), spreading it into a band of noise: a faint fizz, then breath and air. The fundamental is touched least |
| 2 | haze | 0 to 100 cents | 0 | `25c` | Each partial becomes a pair detuned by this much, so it beats: slow chorus at a few cents, fast shimmer and roughness toward a semitone. The note stays in tune |
| 2 | time | 30 ms to 30 s, then hold | 1.9 s | `1.9s` | The time between breath's drift targets: from fast glitter at the bottom to slow evolution at the top. At `hold` the drift freezes where it is (haze and blur keep moving) |

Shape, vowel, breath, blur and haze default to off, so a new sound starts
clean; the presets show the range. Time acts through breath. Partials above
16.7 kHz fade out and nothing at or above 19.9 kHz sounds, so high notes
never alias.

Measured on a held note at 0, 25, 50, 75 and 100 (`tests/sound.cpp` checks
each step):

| Control | 0 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| tilt: centroid, harmonic number | 1.1 | 1.5 | 3.8 | 11 | 16 |
| shape: even/odd partials, dB | +3 | -9 | -97 | +3 (pulse 18%) | +1 (pulse 6%) |
| stretch: partial 2's ratio | 2.00 | 2.21 | 2.45 | 2.71 | 3.00 |
| breath: level drift of partials 1-10, dB rms | 0 | 2.2 | 4.3 | 6.5 | 8.7 |
| blur: power within 6 Hz of a harmonic | 100% | 72% | 47% | 37% | 29% |
| haze: beat rate, harmonics 2-10 of 441 Hz | none | 9 Hz | 38 Hz | 67 Hz | 75 Hz |
| time (breath 100): level change, dB/s | 500 | 97 | 16 | 4.8 | 0 (hold) |

Vowel moves the strongest harmonic of C3 to 262, 523, 785, 523 and 262 Hz for
oo, oh, ah, eh and ee, and ee lifts the harmonics near 2.3 kHz 30 dB over oo.

Loudness is held even across every control: a note's partial levels are
scaled to a fixed root-sum-square, and they sum to at most 22000 (Q15), so
one voice peaks at most at 0.21 of full scale (`outputPeakQ31`). Eight-note
chords of the presets peak at about 0.43 to 0.62.

### Older sounds

This version replaced the controls `odd` and `partials` with `shape` and
`vowel`, moved `breath` to page 2 and changed the ranges of `blur`, `haze`
and `time`. Projects and presets saved with the earlier Bloom load with
different settings and need their sound remade.

## Presets

| Preset | Sound |
| --- | --- |
| Init | Clean, still additive tone |
| Bloom | Slowly breathing, gently detuned pad with a little air |
| Tape Choir | A soft "oh-ah" choir: vowel, chorus and breath |
| Juno Dusk | Pulse strings with drifting width (PWM) and chorus |
| Glass Bells | Stretched, bell-like partials twinkling about four times a second |
| Frost | Bright glitter: every partial flickering at about 50 ms |
| Fog | Breathy noise bands around an "oo" |
| Seasick | Wide detune near a semitone, an "eh" vowel, warped and woozy |
| Still Life | A frozen, sung spectrum: breath held at `hold` |

## Screen

Below the two-page header, a meadow of partials stands under a low striped
sun. Each partial is a stem with a blossom, placed across the screen by its
pitch (log scale) and as tall as its level. The levels and pitches come from
`spectrum.h`, for C3, so the meadow follows the controls the way the sound
does:

- **tilt** slopes the meadow and raises the sun as the tone brightens.
- **shape** shortens the even stems toward square, then cuts the pulse's
  notches into the meadow.
- **stretch** spreads the partials to the right; the highest leave the screen.
- **vowel** raises two hills behind the meadow, one at each formant, and the
  stems under them grow tall. The hills wander with breath.
- **breath** makes the stems rise and fall while notes are held. The shared
  weather lifts and lowers the sun, and pollen drifts up from the blossoms.
- **blur** sets each blossom in a dark halo as wide as the partial's spread,
  which shimmers while notes are held.
- **haze** splits each blossom into its detuned pair, apart by the detune;
  while notes are held the pair swells and shrinks in turn, a slowed-down beat.
- **time** sets how fast the drift moves, and a breeze that nods the meadow.
  At `hold` the scene turns white and stands still, but for a little pollen.

Holding notes opens and brightens the blossoms, and each new note swells
them briefly. A slow breeze and a little pollen keep moving for as long as a
note is held, whatever the controls. When no notes are held the screen stays
still. Motion settles within 0.2 s, and with reduced motion the screen jumps
straight to its end state. The screen keeps its own copy of the drift, timed
from the controls, because it cannot read the audio.

The device redraws what changes, so what moves does so in whole pixels and
stepped sizes, and a frame redraws at most 48 of the meadow's nodes, the partials
furthest from where they should be first, while the sun and the pollen take
turns. The solid blossoms and dark halos still move, because they are the look,
so the fast presets run well over the usual 20 nodes a frame. With a note held
at 60 frames a second:

| Preset | Nodes changed a frame, average | Peak |
| --- | --- | --- |
| Still Life | 1.9 | 49 |
| Init | 2.9 | 38 |
| Bloom | 16 | 50 |
| Fog | 22 | 50 |
| Tape Choir | 25 | 50 |
| Seasick | 37 | 56 |
| Juno Dusk | 42 | 56 |
| Glass Bells | 48 | 56 |
| Frost | 52 | 56 |

The heaviest settings reach 54.3 on average with a peak of 59, and nothing
changes at rest. `tests/churn.cpp` holds it to 60 and 60.

## CPU

Measured on an OP-XY (OS 1.2.1) with the SDK's `profile benchmark`, three
runs: one voice playing middle C with the first four controls at their
midpoint costs **6.24%** of the 580,352-cycle audio block, about
36,200 cycles, so eight voices take about 50%. The OP-XY's own engines
measure 1.6–2.7% a voice in the same test. The test plays one setting;
heavier settings cost more.

Bloom always runs its 24 partials, each a table-read sine at about 11
Blackfin instructions a sample in a zero-overhead hardware loop, so a low
note, with every partial below the cutoff, is its heaviest. High notes cost
less as their upper partials pass the cutoff and stop.

## Files

- `spectrum.h`: the sound's laws as pure functions (controls, drift, the
  per-partial spectrum, haze's and blur's per-partial reach). The display
  includes it and draws what a note does.
- `tables.h`, `sine.h`: lookup tables.
- `engine.cpp`, `core.h`: the voice. Once a block it takes the spectrum, adds
  blur's pitch noise and haze's beat to each partial, then ramps each
  partial's level across the 64-sample block and sums the sines.
- `display.cpp`, `display.h`: the screen. `tests/churn.cpp` checks what it costs.
- `panel.svg.fragment`: the screen's artwork, written by
  `tools/make_panel.py`. Run the script after changing a shape, and commit both.
- `tests/sound.cpp`: the sound checks, run by `sdk.py test`.
