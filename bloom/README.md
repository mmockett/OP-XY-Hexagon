# Bloom

Bloom is an additive pad voice. Each note is a bank of up to 24 sine
partials, and every partial drifts slowly in level and a little in pitch on
its own (seeded, smoothed random walks), so a held chord keeps changing
without the OP-XY's LFOs. Turn the drift off and you have a clean, static
additive tone. Turn it up and you get hazy, slightly detuned, nostalgic
textures.

A track's voices also share one slow "weather" gust. It leans the whole
chord brighter or darker together, so a big chord still audibly breathes
instead of averaging out to a blur. Each note's evolution is deterministic:
the same notes, played in the same order, sound the same.

## Controls

Press any encoder to switch pages. Page 1 shapes the tone; page 2 sets the
size of the spectrum and how it moves.

| Page | Control | Range | Default | Caption | What it does |
| --- | --- | --- | --- | --- | --- |
| 1 | tilt | -24 to +6 dB/oct | -9 | `-9dB` | Spectral slope, from almost a pure sine to bright and glassy. Loudness stays about even across the range |
| 1 | odd | 0 to 100 | 0 | `0` | Removes the even partials, for a hollow, clarinet-like tone; at 100 only odd partials remain |
| 1 | stretch | 2.00 to 2.83 | 2.00 | `2.00` | Inharmonicity: partial *k* sits at *k*^(1+s). The caption shows partial 2's ratio. Low values give a piano-like shimmer, high ones bells and glass |
| 1 | breath | 0 to 100 | 0 | `0` | How far each partial's level drifts, up to ±12 dB, plus the shared weather gust |
| 2 | partials | 1 to 24 | 16 | `16` | How many partials sound. The top one fades in smoothly. Fewer partials also costs less CPU |
| 2 | blur | 0 to 100 | 0 | `0` | Spreads each partial's drift to its neighbours: from single partials sparkling on their own to broad, vowel-like swells |
| 2 | haze | 0 to 30 cents | 0 | `7.5c` | How far each partial's pitch drifts, for detuned, chorus-like beating. Partials 1 and 2 drift less, so the note stays in tune |
| 2 | time | 0.5 s to 30 s, then hold | 8.1 s | `8.1s` | The time between drift targets. At the top of its travel (`hold`) the spectrum freezes exactly where it is |

Breath, haze and blur default to off, so a new sound starts clean; the
presets show the drift. Partials above 16.7 kHz fade out and nothing at or
above 19.9 kHz sounds, so high notes never alias.

## Screen

Below the two-page header, a meadow of partials stands under a low striped
sun. Each partial is a stem with a blossom, placed across the screen by its
pitch (log scale) and as tall as its level. The levels and pitches come from
`spectrum.h`, so the meadow follows the controls the way the sound does:

- **tilt** slopes the meadow and raises the sun as the tone brightens.
- **odd** shrinks the even partials, down to short stubs at 100.
- **stretch** spreads the partials to the right; the highest leave the screen.
- **breath** makes the stems rise and fall while notes are held. The shared
  weather leans the whole meadow and lifts and lowers the sun, and pollen
  drifts up from the blossoms.
- **partials** sets how many stems there are.
- **blur** puts a halo round each blossom while notes are held, wider as the
  drift spreads to neighbours.
- **haze** makes the blossoms waver sideways while notes are held.
- **time** sets how fast the drift moves. At `hold` the scene turns white and
  stands still.

Holding notes opens and brightens the blossoms, and each new note swells
them briefly. When no notes are held the screen stays still. Motion settles
within 0.2 s, and with reduced motion the screen jumps straight to its end
state. The screen keeps its own copy of the drift, timed from the controls,
because it cannot read the audio.

## Files

- `spectrum.h`: the sound's laws as pure functions (controls, drift, the
  per-partial spectrum). The display can include it and draw what a note does.
- `tables.h`, `sine.h`: lookup tables.
- `engine.cpp`, `core.h`: the voice. It ramps each partial's level across the
  64-sample block and sums the sines.
- `display.cpp`, `display.h`: the screen.
- `panel.svg.fragment`: the screen's artwork, written by
  `tools/make_panel.py`. Run the script after changing a shape, and commit both.
- `tests/sound.cpp`: the sound checks, run by `sdk.py test`.
