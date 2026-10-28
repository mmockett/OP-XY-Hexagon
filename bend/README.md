# Bend

Phase distortion after the Casio CZ. Each note plays two lines, and each line
reads a cosine through a bent phase map. Bend (the CZ's DCW) moves every map
from the straight line, a pure sine, to the chosen wave: saw, square, pulse,
double sine, saw-pulse, or one of three resonant waves that sweep like a
resonant filter. Every note also carries its own DCW contour: it starts
brighter, settles over a second and breathes while held, so the reso waves
wah and the bent waves pluck. Line 2 sounds at an interval, detuned, and ring
blends it into its product with line 1. Warp goes beyond the CZ with sync-like
sweeps, noise modulates the phases as the CZ's noise does, and lofi plays the
voice through a coarse DAC.

With the colouring controls at zero (detune, ring, warp, noise, lofi) Bend is
a clean CZ saw at bend 25: two lines in unison, plucked by the contour. The
OP-XY's own envelopes, filter, LFOs and effects do the rest.

## Controls

Press any encoder to switch pages.

| Page | Control | Range | Default | Header | What it does |
| --- | --- | --- | --- | --- | --- |
| 1 | wave | saw, sq, pulse, dsine, sawpl, res1, res2, res3 | saw | the wave's name | The map both lines are bent towards: the CZ's eight waves, numbered as the CZ numbers them. One choice per detent. |
| 1 | bend | 0 to 100 | 25 | amount | The CZ's DCW: how far the map bends, from the straight line (a pure sine on every bent wave) to the full wave. Each note's contour adds to it (below). |
| 1 | detune | 0 to 40 cents (40 x²) | 0 | `2.5c`, `10c`, `40c` | Line 2 against line 1. At unison the lines sit half either side of the note, so the chorus stays in tune; at other intervals line 1 stays on the note and line 2 rises. |
| 1 | ring | 0 to 100 | 0 | amount | Blends line 2 into its product with line 1 (ring modulation): sum and difference tones, bells and clangs at the wider intervals. |
| 2 | interval | -oct, uni, 4th, 5th, m7, oct, 12th (an octave and a fifth), 2oct | uni | the interval | Line 2's pitch: an octave below to two octaves above line 1. |
| 2 | warp | 0 to 100 | 0 | amount | Beyond the CZ: skews the input phase (slow start, hurried end) and overruns the output up to 2.5 cycles, so the cosine wraps before the cycle ends, as hard sync does. Each turn is a new sync timbre. |
| 2 | noise | 0 to 100 | 0 | amount | The CZ's noise: the lines' phases move by low-passed noise (at 8 times the note, so it is pitched with the keyboard), rms 0.015, 0.045, 0.1 and 0.22 of a cycle at 25, 50, 75 and 100. |
| 2 | lofi | 48 kHz, 16 bits to 4 kHz, 4 bits | 0 | the DAC's rate: `48k`, `23k`, `5.8k` | A coarse DAC: the voice held at a rate falling from 48 to 4 kHz (48 x 12^-x) and stepped to a depth falling from 16 to 4 bits, without anti-aliasing. |

Measured on a held note (`tests/sound.cpp` checks every step):

| Control | 0 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| bend, saw at 220 Hz: spectral centroid | 220 Hz | 247 | 291 | 364 | 427 |
| bend, pulse at 220 Hz: centroid | 220 Hz | 292 | 440 | 731 | 973 |
| bend, res2 at 220 Hz: centroid | 260 Hz | 604 | 842 | 1081 | 1320 |
| detune: unison beat at 900 Hz | none | 1.3 Hz | 5.2 | 11.7 | 20.8 |
| ring (at the 5th): change from ring 0 | — | -13 dB | -7 | -3.6 | -1.1 |
| warp (saw, bend 50): change from the last quarter | — | 17 dB | 7.4 | 7.5 | 9.6 |
| noise: power off the harmonics | -78 dB | -21 | -12 | -5 | -1 |
| lofi: the DAC's error | none | -29 dB | -22 | -15 | -10 |
| lofi: rate, depth | 48 kHz, 16 bits | 26 kHz, 13 | 14 kHz, 10 | 7.4 kHz, 7 | 4 kHz, 4 |

Every wave differs from the last by 10 to 13.5 dB (the rms difference of
their log spectra at bend 60), and every interval sounds line 2 at its ratio,
as loud as line 1.

## The waves

A line's input phase runs once a cycle; its map gives the output phase, and
the line plays -cos of it. bend's share d moves every map from the straight
line to:

- **saw**: half a cycle in the first (1 - d) / 2 of the cycle, the other half
  over the rest.
- **sq**: a rise to the half, a hold, a rise to the whole, a hold; each rise
  (1 - d) / 2 long.
- **pulse**: a hold at the start, then the whole cycle in the last 1 - 0.9 d.
- **dsine**: the straight line folded at the quarters, so the sine doubles.
- **sawpl**: half a cycle in the first 4%, the rest by the half, then a hold.
- **res1, res2, res3** (the CZ's reso waves): the cosine runs at 1 + 5 d times the note under a
  saw, triangle or trapezoid window, which resets every cycle: a resonance
  that sweeps up with bend. At bend 0 they are still windowed (soft, not pure
  sines), as on the CZ.

The saw, square and pulse steepen as 1 / (1 - d), so bend reaches them
through 1 - (1 - bend)², which spreads their brightening evenly over the turn.

### The DCW contour

Each note brings its own contour, as on the board: it rises within 35 ms, to
about half of what bend leaves, decays over 0.8 s to 0.14 of it and breathes
±0.07 at 0.27 to 0.33 Hz (each note its own rate) while held. A note played
again restarts it from where it was, so it never jumps. The contour is a
fixed part of the voice, not a control: the OP-XY's envelopes and LFOs are
its own pages, and the contour only shapes how each note speaks.

Unlike the board, the contour fades in over bend's first fifth: at bend 0
every bent wave is a still, pure sine, and at bend 100 there is nothing left
to add. In between a note always plucks a little brighter than the knob, which
is what makes the reso waves wah and a CZ brass bite.

### Band limits

The maps are evaluated exactly, sample by sample, with no table to smear
them; their sharpest parts are what could alias. So each line bends no further
than its pitch allows: the steepest part of a bent map runs the cosine at most
at an eighth of the sample rate, a reso wave's smooth resonance at most at a
fifth, and the whole turn of bend is scaled into what is allowed, so high
notes bend less but every part of the turn still bends them. The steps a line
makes where its cycle ends (warp's overrun, the reso windows' edge) are
smoothed by a two-sample polynomial BLEP. Below 12 kHz, little folds back: at
worst -49 dB at C4, -43 dB at C6 and -41 dB at C7 over every wave, bend and
warp. Without warp the saw, square and pulse stay below -55 dB at C7 (dsine
and sawpl, with sharper corners, -44 and -49 dB). lofi aliases on purpose.

### Level

The bent waves carry DC (the pulse most), which a one-pole blocker at 6.9 Hz
removes; a new voice's blocker starts at its waves' mean, so a note does not
thump, and its level glides in over its first block, so it does not click.
Each voice starts at its own place in the cycle, so a chord struck at once does
not peak at once. The reso waves play 3.5 dB louder (1.5 times; reso3 1.25)
for the power their windows take. A voice peaks at most at 0.1875 of full
scale (`outputPeakQ31`, guarded by a clamp that engages less than once in a
million samples, and only at the harshest corners). Eight-note chords of the presets
peak between 0.42 and 0.71 of full scale; the loudest corner of the controls
reaches 0.96.

## Presets

| Preset | Sound | Eight-note peak |
| --- | --- | --- |
| Clean | The init sound: a CZ saw in unison, plucked by the contour | 0.71 |
| Haze | The board's: res2 an octave up, detuned and a little rung, warped, noisy and coarse | 0.50 |
| Brass 101 | A bitey CZ brass: bent saws 5 cents apart | 0.51 |
| Wah Bass | res1 with line 2 an octave down: each note wahs | 0.52 |
| Glass Ring | dsine rung against an octave and a fifth: bells and glass | 0.42 |
| Sync Lead | A square warped into sync, line 2 a fifth up | 0.54 |
| Breath | A narrow pulse an octave up in noise: a breathy flute | 0.46 |
| Toy DAC | sawpl through a 6.6 kHz, 6-bit DAC, a little rung and warped | 0.45 |

## Screen

Ported from the design board. Below the two-page header:

- **phase**, the owner's-manual figure: input phase across, output phase up,
  graph paper at the quarters, the straight map dashed and the bent map bright.
  It is the map the newest note is playing, contour and all, wrapped to one
  cycle with each wrap a vertical drop; noise jitters it. A new wave fades in
  over the last in 0.15 s.
- **cos**: the cosine the map is read through.
- **wave**: line 1 as 160 dots over two of its cycles, line 2 as 69 fainter
  dots at its interval (gliding as the interval changes), sliding against line
  1 at a quarter of detune's beat, in 24ths of a cycle, and turning into the
  ring product as ring rises (brighter past half). lofi holds and steps the
  dots as its DAC would; noise jitters their phases by its rms.
- **dcw**: a dial for each held note (up to eight), lit while it is held,
  showing bend plus that note's contour, to the nearest degree.
- The eight waves, the chosen one lit on its key, which slides to it.

The screen is still until you play or turn something: the contours, line 2's
slide and the noise's roll (4 times a second) run only while notes are held.
Every turn eases out within 0.2 s; reduced motion and a track change jump
straight to the end state.

The OP-XY redraws whatever changes in a frame, so the screen changes little
while notes alone move it: the map follows the contour to 1/256 of bend, the
dots sit on whole pixels and follow it in steps of 1/32, and a sixth of the
dots is redrawn each frame (all of them while a control eases). With Haze,
Wah Bass or Clean held, a frame changes 8 to 15 elements on average and at
most about 40.

The display keeps its own copy of what the sound is doing, from the controls
and elapsed time, because it cannot read the audio; its maps and waves are
`pd.h`'s, the same functions the engine plays.

### Budget, and where it departs from the board

256 nodes, the most a screen may have: the two map outlines (64 points
each), the header's 8 bar steps, 160 + 69 dots, 8 needles, 8 wave icons and
the key; the 16 text slots are the header. Everything else is static artwork,
written by `tools/make_panel.py` from the board's geometry. To fit:

- Line 2 has 69 dots (the board's 72), and the dials' hubs are static (the
  board lights them with the needle).
- The map is simplified to 32 points from 64 samples (the board's 160), and
  drawn as a 0.6 px stroked ribbon, since an outline the frame reshapes has
  no fill.
- The figure and scope draw the engine's maps, so the saw, square and pulse
  look more bent than the board's at the same bend (bend's taper), and the
  scope's noise follows the sound's depth (the board's grew to 0.06 of a cycle).
- The dials show the contour fading in over bend's first fifth, as the sound
  does.
- Turns ease in 35 ms (the board's 45), so that everything settles within
  0.2 s; the dots move in whole pixels and refresh a sixth at a time while
  notes play, rather than easing every frame; line 2 slides at a quarter of the
  board's speed, in steps, and the noise rolls 4 times a second (the board's
  14).
- The CZ's reso waves are captioned res1, res2 and res3, and the interval of
  an octave and a fifth 12th, to fit the header's value column.
- Labels are authored text in the OP-XY's font (the board's Inter).

lofi's header shows the DAC's rate, which falls as lofi rises, as the board
writes it.

## For the screen

`pd.h` holds the sound's laws as pure functions: `shape` (a wave's map at a
bend and warp), `output` (a line's value at a phase), `contour` and `bent` (a
note's DCW), `ceiling` (the band limit), and the controls' units for the
captions (`detune_tenths`, `lofi_rate_tens`, `lofi_bits`, `noise_depth`).

## Files

- `pd.h`: the phase maps, the contour and the controls' units, shared by the
  engine and the screen.
- `tables.h`: the cosine and powers of two, written by `tools/make_tables.py`.
- `engine.cpp`, `core.h`: the voice.
- `display.cpp`, `display.h`: the screen.
- `panel.svg.fragment`: the screen's artwork, written by
  `tools/make_panel.py`. Run the script after changing a shape, and commit both.
- `tests/sound.cpp`: the sound and screen checks, run by `sdk.py test`.

## CPU

Measured on an OP-XY (OS 1.2.1) with the SDK's `profile benchmark`, three
runs: one voice playing middle C with the first four controls at their
midpoint costs **2.58%** of the 580,352-cycle audio block, about
14,900 cycles, so eight voices take about 21%. The OP-XY's own engines
measure 1.6–2.7% a voice in the same test. The test plays one setting;
heavier settings cost more.

Each line runs in two passes over the block, pointer walks that the Blackfin
compiler turns into zero-overhead hardware loops with nothing spilled to the
stack: the map pass (9 to 33 instructions a sample, the most for dsine or
square with warp) and the cosine pass (16 to 23, the most with noise under a
reso window). Noise adds a 10-instruction pass, and the mixing pass (ring, DC
blocker, lofi, level) takes 24 to 36. At the heaviest setting (warp, noise,
ring and lofi together) that is about 160 instructions a sample, 10,100 a
block, plus about 1,200 for planning the block and smoothing the steps: about
11,300 Blackfin instructions per voice block by static count. The SDK's host
estimate (`sdk.py test`) is about 2,400 to 3,700.
