# Orbit

A vector synth, after the Prophet VS and the Wavestation. Four single-cycle
waves sit at the corners of a diamond: A at the top, B on the right, C at the
bottom and D on the left. Each comes from one of eight banks of old digital
waves. A point inside the diamond mixes the four, each corner weighted by how
near the point is, as on the VS. The point orbits a centre that you set with x
and y. Its path is a circle, a tilted ellipse, a figure 8 or steps from corner
to corner (a wave sequence), and rate sets how fast it goes. Each note's orbit
starts at the top of its path, so notes struck together move together. Grit
plays the result through an old DAC: a zero-order hold whose rate falls to
10 kHz, and bits down to 8, which alias as the VS and the PPG did. Detune
doubles each note into two sets a few cents apart.

With orbit, grit and detune at zero (the defaults), Orbit is a clean, still
tone: glass's four waves mixed evenly, band-limited, with nothing between the
harmonics. The OP-XY's own envelopes, filter, LFOs and effects do the rest.

## Controls

Press any encoder to switch pages.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | bank | rate |
| 2 | x | path |
| 3 | y | grit |
| 4 | orbit | detune |

| Page | Control | Range | Default | Header | What it does |
| --- | --- | --- | --- | --- | --- |
| 1 | bank | glass, vox, organ, reed, bell, pluck, wire, dusk | glass | the bank's name | The four corner waves. One choice per detent. A change mid-note fades across one block. |
| 1 | x | -100 to 100 | 0 | `-20` | The orbit's centre, left (D) to right (B). |
| 1 | y | -100 to 100 | 0 | `16` | The orbit's centre, down (C) to up (A). |
| 1 | orbit | 0 to 100 | 0 | amount | The path's radius, up to 0.85 of the way to a corner. A point that would leave the diamond rides along its edge. |
| 2 | rate | 0.02 to 8 Hz, exponential | 0.40 Hz | `.40Hz`, `1.8Hz` | How fast the point goes round: 0.02, 0.089, 0.40, 1.8 and 8 Hz at 0, 25, 50, 75 and 100. |
| 2 | path | circle, ellipse, figure 8, steps | circle | `circ`, `ellip`, `fig8`, `steps`, or `c/el`-style between two | The path's shape, morphing evenly between neighbours: a circle clockwise from the top, an ellipse 0.42 wide tilted 0.6 rad, a figure 8 through the centre, and steps that hold at each corner and jump in the last sixth of each step. |
| 2 | grit | clean, then 48 kHz down to 10 kHz | clean | `clean`, `32k`, `14.8k`, `10.0k` | An old DAC. Its rate falls exponentially from 48 to 10 kHz while its bits fall from 16 to 8. Below 44.1 kHz each set's wave is sampled at the DAC's rate and held, so it aliases. Above 44.1 kHz, the first twentieth of the turn, only the bits act. |
| 2 | detune | 0 to 30 cents (by the square of the turn) | 0 | `9.1c` | A second set of the same mix: 1.9, 7.5, 17 and 30 cents apart at 25, 50, 75 and 100, the two sets either side of the note. It comes in over the first eighth of the turn, and the note gives way to it, so their power stays even. |

As on the board, grit's header shows the DAC's rate, which falls as grit
rises.

### What each control does, measured

`tests/sound.cpp` measures each control at 0, 25, 50, 75 and 100 and checks
each step. These are its figures, on a 220.5 Hz note of glass, centred, unless
said otherwise:

| Control | 0 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| x: spectral centroid, harmonics | 2.66 | 1.67 | 1.27 | 1.19 | 1.30 |
| x: timbre moved by each quarter (0 same, 1.41 nothing shared) | | 0.32 | 0.18 | 0.08 | 0.13 |
| y: spectral centroid, harmonics | 1.81 | 1.41 | 1.27 | 1.12 | 1.00 |
| y: timbre moved by each quarter | | 0.29 | 0.17 | 0.12 | 0.19 |
| orbit: the point's reach | 0 | 0.21 | 0.43 | 0.64 | 0.85 |
| orbit: the timbre's swing over a 1 Hz cycle | 0.001 | 0.052 | 0.107 | 0.169 | 0.219 |
| rate: the vector's measured cycle, Hz | 0.020 | 0.089 | 0.400 | 1.79 | 8.00 |
| path: the trace moved by each quarter (orbit 100) | | 0.35 | 0.28 | 0.24 | 0.52 |
| grit: aliasing (power between the harmonics), dB | -81 | -33.5 | -30.2 | -27.0 | -23.8 |
| detune: the fundamental's beat, Hz (all near full depth) | none | 0.24 | 0.95 | 2.14 | 3.82 |

The four paths at orbit 100: the circle spans 1.70 × 1.70, the ellipse
1.12 × 1.43, the figure 8 1.70 × 0.85 and crosses the centre twice a cycle,
and steps dwell at the corners 83% of the time. The eight banks' centroids run
from 1.13 (dusk) to 4.11 (wire) harmonics, and no two banks are closer than
0.27.

## The banks

Each bank's four waves are the design board's: 64 samples each, their DC
removed and their peaks normalised, as a vintage wave ROM would hold them.

| Bank | A | B | C | D |
| --- | --- | --- | --- | --- |
| glass | sine | 1:2 FM | sine with its 4th and 6th | tine |
| vox | glottal pulses ringing a formant: oo | oh | ah | ee |
| organ | drawbars 1-2-3 | 1-2-4-8 | 1-3-5-6-8 | a principal |
| reed | narrow pulse | band-limited square | nasal peak at the fifth | clarinet |
| bell | deep 1:3 FM | partial cluster | light 1:5 FM | hollow chime |
| pluck | saw | bright band-limited saw | hard-synced sine | chirp |
| wire | sync at 1.5 | sync at 2.5 | synced sine at 3.7 | wavefold |
| dusk | triangle | soft saw | hollow square | sine with octave and fifth |

## How it works

`tools/make_tables.py` resynthesises each wave from its 64 samples' harmonics
at 128 points, at five band limits: 31, 15, 7, 3 and 1 harmonics. It writes
them to `waves.h`, with each band's products of corners and each corner's
peak.

Once per 64-sample block, the voice:

1. Finds its point: the centre, plus the path at the orbit's phase, within
   the gate.
2. Weighs the four corners bilinearly.
3. Picks the band for the note. Up to 419 Hz it uses all 31 harmonics; above
   that it crossfades to half the harmonics each octave, so no note aliases
   (grit apart).
4. Mixes the corners into a 128-point wave for the block. It does this only
   when the mix has moved, so a still sound costs little.
5. Sets the wave's loudness from its power, worked out from the weights and
   the band's products of corners. Moving the point keeps the level even.
   Peaky waves are held so the mix's loudest possible point stays within the
   voice's peak.

Each sample then reads that wave, interpolated: once for the note and, with
detune, again for its twin. With grit below 44.1 kHz, each set's phase moves
only when the DAC's clock ticks, so the wave is sampled at the DAC's rate and
held. Any change of wave or gain fades in across one block, from where the
last block left off, so the moving vector, a bank change and a note restart
never click. The largest step at a block's edge is no larger than one within
a block.

Integer arithmetic only, 32-bit, with 16 × 16 multiplies in the sample loops.
Nothing is shared between voices.

### Level

One voice peaks at most at 0.203 of full scale (`outputPeakQ31`, 436000000).
A set peaks at 0.144, and the twin adds up to 1.4 times that. The fades are
held within the same ceiling, and the grit's rounding adds at most half a
step. Each preset's eight-note chord (C3 to G5, the voices' raw sum through
the SDK's browser engine) peaks at:

| Preset | 1 note | 8 notes |
| --- | --- | --- |
| Init | 0.093 | 0.652 |
| Clean | 0.098 | 0.653 |
| Dusty | 0.180 | 0.540 |
| Drawbars | 0.189 | 0.706 |
| Chimes | 0.177 | 0.739 |
| Wave Seq | 0.168 | 0.744 |
| Old DAC | 0.131 | 0.545 |
| Dusk Pad | 0.163 | 0.691 |

The loudest still corner of any bank, with full detune, peaks at 0.87 for
eight notes.

## Presets

| Preset | Sound |
| --- | --- |
| Init | The defaults: glass, centred and still. Clean |
| Clean | The board's: glass, a slow round orbit at 0.4 Hz |
| Dusty | The board's: vox off-centre, a figure 8 at 0.82 Hz, a 20 kHz DAC and 9 cents of detune |
| Drawbars | Organ drawbars drifting round a slow ellipse, gently doubled |
| Chimes | Bell waves circling, a touch of grit and detune |
| Wave Seq | Pluck waves stepped corner to corner at 2.1 Hz: a wave sequence |
| Old DAC | Wire waves through a 12 kHz, 9-bit DAC, slowly turning |
| Dusk Pad | Dusk's soft waves in a very slow figure 8, widely detuned |

## Screen

The screen is the design board's (`Orbit.dc.html`), at the OP-XY's 480 × 220.

- **The field** (left) is the diamond gate, with its half-size inner diamond
  and dotted axes. Each corner's wave is drawn beside it. Each wave brightens
  and swells with its share of the mix, and the gate's corner ticks brighten
  with it. On a bank change the old waves shrink as the new ones grow. From
  half grit the waves are drawn in steps.
- **The orbit** is dotted round its centre, marked with a cross, and moves as
  x, y, orbit and path turn. With steps, the four stations are ringed.
- **Each held note** (up to eight) sets a point travelling the path from its
  top, at the rate. The point drops dots behind it that stay where they fall
  and dim in steps, so its trail never reaches further back than the note has
  played. The newest point is brightest, has a ring, and a dotted spoke joins
  it to the centre; older points keep four dots to its eight. A ping spreads as
  each note starts. Released, a point coasts to a stop and fades in about
  0.15 s. At rest, a hollow ring marks where the next note will start.
- **The scope** (right) draws one cycle of the wave the newest point mixes,
  normalised, on a graticule. Grit holds it in steps and coarsens its levels;
  both are exaggerated so they can be read. Detune draws the twin behind it,
  drifting at a pace set by the detune. The body under the wave is a comb of
  upright strokes. For as long as a note sounds, a bright bead sweeps the wave.
- **Pips** under the scope show each corner's share in eighths.

The screen cannot hear the voices. It moves each point from the controls
and the time since its note started, through the same path and weight
functions the audio uses (`vector.h`). Every turn eases in about 0.15 s.
When nothing plays and nothing turns, the screen is still. Reduced motion
holds the points at the top of the path and drops the pings. A reset or a
track change shows the end state at once. `NoteTracker` follows the notes.

The device redraws what changes, so `display.cpp` spends a budget: at most 18
nodes change a frame, however many notes sound. Each part works out where it
should be and asks; what the budget refuses asks again next frame. Points move
by whole pixels, the scope redraws one outline a frame, and notes struck
together draw once. `tests/churn.cpp` holds it to 20 a frame on average with a
peak of 24, at every control value and at 1, 4 and 8 notes, and none at rest; it
measures 17.7 and 19 at the heaviest.

`tools/make_panel.py` writes `panel.svg.fragment` with the node numbers
`display.h` names. The budget used:

- **Outlines:** 3 of 64 points: the scope's wave, its twin and the body's
  comb. They span 204 px, past an outline's 128, so they are authored at
  1/1.62 and placed at 1.62 times. Each ribbon's ends are cut square to the
  line, so no turn comes within 5 degrees of straight back.
- **Nodes:** 253 of 256. These are the 8 header-bar nodes; the 64 corner
  waves (eight banks × four corners, smooth and stepped); 4 gate ticks; 48
  orbit dots; 4 stations; the cross; 6 spoke dots; 8 trails of 8; 8 points;
  the ring; 8 pings; the rest ring; 32 pips; and the scope's bead.
- **Text slots:** all 16, for the two-page header.
- **Palette:** the board's 16 greys, each stroked 0.45 px. Filled shapes are
  authored that much smaller.

The artwork is 64.4 KB of its 65.5 KB (65,536 bytes). The corner waves are
authored at their true size to a tenth of a pixel. The labels A to D are static
text, at 8 px.

Departures from the board:

- The body under the scope's wave is a comb of strokes, not a solid fill,
  because the OP-XY's mutable outlines cannot be filled.
- A corner wave has 20 points, not 40, and a stepped one 8 steps, to keep the
  artwork within 64 KB.

## CPU

Measured on an OP-XY (OS 1.2.1) with the SDK's `profile benchmark`, three
runs: one voice playing middle C with the first four controls at their
midpoint costs **2.39%** of the 580,352-cycle audio block, about
13,900 cycles, so eight voices take about 19%. The OP-XY's own engines
measure 1.6–2.7% a voice in the same test. The test plays one setting;
heavier settings cost more.

Every sample loop compiles to a Blackfin zero-overhead hardware loop (checked
in `bfin-elf-objdump -d voice.o`):

| Loop | Instructions | Runs |
| --- | --- | --- |
| A band's mix | 13, or 16 for the second band | 128 points, only when the mix moves |
| One set | 15, or 16 added to the other | 64 samples |
| One set through the DAC | 17, or 18 added | 64 samples |
| The DAC's clock | 5 | 64 samples, grit below 44.1 kHz |
| The DAC's bits | 8 | 64 samples, any grit |
| The fade | 6 | 64 samples, when anything changed |

At the heaviest setting, the loops run to about 7,200 instructions a block:
a moving vector on a note between bands (both bands mixed), detune and grit
below 44.1 kHz. Counting every straight-line instruction in `render` as well,
whichever branch it sits on, gives about 9,200, an upper bound.

The SDK's host estimate is about 1,400. It sweeps one control at a time from
the defaults, where the mix is still.

## Files

- `vector.h`: the controls and their units, the paths, the corner weights,
  the loudness and the captions, as pure functions. The voice and the screen
  share it.
- `waves.h`: the wave ROM, written by `tools/make_tables.py`.
- `engine.cpp`, `core.h`: the voice.
- `display.cpp`, `display.h`: the screen.
- `panel.svg.fragment`: the screen's artwork, written by
  `tools/make_panel.py`. Run it after changing a shape or a wave, and commit
  both.
- `tests/sound.cpp`: the sound and screen checks, run by `sdk.py test`.
- `tests/churn.cpp`: the screen's cost and outlines, run by `sdk.py test`.
