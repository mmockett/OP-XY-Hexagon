# Moth

A west-coast complex oscillator, after Buchla's. A sine carrier is
frequency-modulated by a second sine (fm sets the index, ratio the
modulator's frequency), folded by a sine wavefolder (fold, with bias for
asymmetric, even harmonics), then sent through a lowpass gate modelled as a
vactrol: an LED that follows the note and flashes when it is struck, lighting
a photocell that opens in a millisecond and closes slowly at glow's pace.
Mode sets whether the gate closes a low-pass (lp), fades the level (vca) or
both (combo). Strike sets how hard each note hits the LED: at 0 the gate
simply holds open while the note is held, a pad; at 100 every note is a
sharp ping that dies away, a pluck or a bongo. Drift lets the oscillators
and the folder wander slowly, as analogue ones do.

The vactrol is the instrument's own character, not an envelope control: it
is how this voice speaks. The OP-XY's amp envelope still applies on top.

## Controls

Press any encoder to switch pages. Page 1 shapes the tone, page 2 the gate
and the drift.

| Encoder | Page 1 | Page 2 |
| --- | --- | --- |
| 1 | fold | glow |
| 2 | fm | bias |
| 3 | ratio | mode |
| 4 | strike | drift |

| Control | Range | Default | Caption | What it does |
| --- | --- | --- | --- | --- |
| fold | drive 0.02 to 6.82 radians | 20 | amount | The folder, sin(A (x + b/2)) scaled so its peak is 1. At 0 the carrier passes untouched (harmonics 94 dB down); past a quarter turn of drive every further half turn adds a fold, about four at 100. |
| fm | index 0 to 4 (4 x²) | 0 | index, `0.36` | How far the modulator moves the carrier's phase: from a pure sine, through the first sidebands, to a dense FM spectrum. |
| ratio | 0.50, 1.00, 1.01, 1.41, 1.50, 2.00, 2.02, 2.50, 2.76, 3.00, 3.50, 4.00, 4.70, 5.00, 6.00, 7.00 | 1.00 | the ratio, `3.50` | The modulator's frequency over the carrier's, sixteen stops: harmonic (0.50, 1, 1.5, 2...), near-harmonic (1.01 and 2.02 beat slowly) and inharmonic (1.41, 2.76, 4.70: bells and bongos). It glides between stops over a few milliseconds. Heard through fm. |
| strike | 0 to 100 | 0 | amount | How hard a note strikes the vactrol. The LED is held at 1 - strike while the note is held, and every strike above 0 pulses it fully for 4 ms, so the gate pings over the held level: 2.5 dB at 25, 6 dB at 50, 12 dB at 75, a pluck that dies away at 100. A harder strike is also up to 1.6 dB louder. Below 12.5 the pulse is gentler, so a light touch eases in. |
| glow | 40 ms to 6.0 s, exponentially | 1.3 s | time, `180ms` or `1.3s` | The photocell's memory: how long a struck gate takes to close, from fully open to -60 dB (in vca mode; it is measured within a third of the caption at every setting). It closes three times faster while still bright and slower as it darkens, as a vactrol does. An unstruck note opens at a twentieth of glow's pace, so a long glow also swells in (2 ms at 0, 0.6 s at 100). |
| bias | 0 to 100 | 0 | amount | Offsets the folder's input by up to half its swing, so the wave folds one way more than the other and gains even harmonics. Its offset is predicted (sin(c) J0(A)) and removed, so a biased note starts without a thump. |
| mode | lp to combo to vca | combo | the nearest of `lp`, `both` (combo) and `vca` | How the gate acts: at lp it only closes a low-pass, at vca it only fades, at combo it does both, and between them each follows the cell's light to that power, so the morph is even in decibels. The low-pass is two one-poles whose cutoff follows the light (open from 0.8 up, about 9 kHz times the light below, never under 3 Hz). |
| drift | 0 to 25 cents, 12.5 (x + x²) | 0 | cents, `3.9c` | Slow seeded wander (0.11 to 0.16 Hz and 0.29 to 0.39 Hz, each voice its own): the pitch up to the cents shown, the modulator on its own as far again, so FM's sidebands drift against the carrier, and the fold's drive up to 25% either way. |

Fm, bias and drift colour the sound, so they default to 0: the init sound,
Clean, is a soft, held sine pad. Ratio is heard through fm, and glow and mode
act through the gate, so they are heard as soon as fm is up or the gate
moves (strike above 0, or an unstruck note swelling in).

Measured on one held note of 220 Hz at 0, 25, 50, 75 and 100, the other
controls at the defaults except where a control acts through another
(`tests/sound.cpp` checks each step; `MOTH_REPORT=1` prints these):

| Control | 0 | 25 | 50 | 75 | 100 |
| --- | --- | --- | --- | --- | --- |
| fold: highest harmonic within 40 dB | 1 | 3 | 7 | 9 | 11 |
| fold: mean harmonic | 1.0 | 1.05 | 2.7 | 2.8 | 5.3 |
| fm (ratio 2.00): highest harmonic within 40 dB | 1 | 3 | 7 | 11 | 15 |
| ratio (fm 50): stops 0.50, 1.50, 2.76, 4.70, 7.00 | first sidebands at 0.5 and 1.5 f0 | at 0.5 and 2.5 | at 1.76 and 3.76 (42% of the power off the harmonics) | at 3.7 and 5.7 | at 6 and 8 |
| strike: ping over the held level | 0 dB | 2.5 dB | 6.0 dB | 12.1 dB | a pluck, gone by 1.5 s |
| glow: a pluck's fall to -60 dB (vca) | 0.05 s | 0.15 s | 0.51 s | 1.8 s | 6.2 s |
| glow: an unstruck note's rise to 90% | 9 ms | 20 ms | 59 ms | 0.19 s | 0.63 s |
| bias: even harmonics against odd | -102 dB | -24 dB | -17 dB | -13 dB | -10 dB |
| mode (a pluck, fold 30): level 200 ms in | -6 dB | -19 dB | -33 dB | -28 dB | -28 dB |
| mode: its overtones over 500 Hz then, of the open tone's | 14% | 15% | 15% | 87% | 100% |
| drift: the pitch's wander, rms | 0 | 2.0 cents | 4.8 cents | 8.5 cents | 12.9 cents |

Combo is the darkest and quickest to fade, as both act at once.

## Presets

| Preset | Sound | 8 notes, peak |
| --- | --- | --- |
| Clean | The init sound: a soft sine pad, held open, easing in | 0.78 |
| Ping | The board's sound: folded FM at 3.50, struck hard over a low held level | 0.45 (struck together) |
| Bongo | Inharmonic FM at 2.76, biased, a short combo pluck | 0.41 (struck together) |
| Marimba | FM at 4.00, struck between lp and combo, a woody decay | 0.59 (struck together) |
| Bell | FM at 1.41, a long ring between combo and vca | 0.78 (struck together) |
| Lantern | A pad: folded, biased, beating at 2.02, swelling in through lp, drifting | 0.78 |
| Dusk | A deep fold, half bias, beating slowly at 1.01, a little strike, wide drift | 0.62 |
| Wingbeat | A sub-octave FM growl (0.50), folded hard, plucked over a held level | 0.57 |

The peaks are the raw sum of an eight-note chord (C3 to G5), full scale 1:
held from 1 s for the pads and over the first seconds for the plucks, whose
notes all strike at once. One voice peaks at 0.1 to 0.15.

## How it works

### Oscillators and the folder

The modulator, the carrier and the folder run at twice the rate, 88.2 kHz,
each in a pass of its own over the 128 oversampled samples of a block. The
carrier is a sine phase-modulated by the modulator (phase modulation, as a
complex oscillator's through-zero FM sounds). The folder is sin(A x + c), read
from a sine table with interpolation; its gain is planned once a block so its
peak is 1 (1 / sin(A (1 + b/2)) below a quarter turn of drive), so fold 0 is
a clean sine and every fold sounds about as loud. Its mean, sin(c) J0(A) for
a sine at its input, is subtracted as soon as the note starts, and a one-pole
follower at 14 Hz takes what that misses.

### Aliasing

Folding and FM spread a sine's energy far up the spectrum, so:

- Oversampling: an elliptic half-band (order 9, two paths of two allpasses)
  brings the oscillators down. It is flat to 20 kHz, 44 dB down at 25 kHz and
  at least 63 dB down from 26 kHz up, so what it folds back lands above
  20 kHz or 63 dB down.
- A governor: the folded FM's bandwidth by Carson's rule,
  f0 (1 + (I + 1) r)(A + 1), is held under 45 kHz. Near the top of the
  keyboard the drive and the index give way equally, in octaves (the folds
  and FM thin out as a real oscillator's would), and the modulator's index
  fades out as the modulator rises from 16 to 22 kHz.

Measured as the power off the harmonics, worst of C3, C5, C6, C7 and C8:

| Setting | below 15 kHz | in all |
| --- | --- | --- |
| fold 100 | -71 dB | -49 dB |
| fold 100, bias 100 | -71 dB | -50 dB |
| fm 100, ratio 1.00 | -67 dB | -53 dB |
| fm 100, ratio 7.00 | -56 dB | -26 dB |
| fold 100, fm 100, ratio 7.00 | -71 dB | -28 dB |
| Ping held (fold 55, fm 30, ratio 3.50) | -80 dB | -41 dB |

What is left lands mostly above 15 kHz, folded back from the half-band's
transition band; below 15 kHz every setting tested is at least 56 dB down,
and most more than 80.

### The vactrol

Each voice keeps its photocell's light (`laws.h`, `gate_step`), updated once a
block and ramped across it. A note's start pulses the LED fully for 4 ms (more
gently below strike 12.5), driving the cell open with a 1 ms time constant;
then the cell settles toward the held level, 1 - strike: up at a twentieth of
glow's pace, down at glow's (three times faster while fully lit, as the
CdS cell's resistance falls). A note struck again while the cell still glows
opens from where it is. The light sets the low-pass (two one-poles, their
coefficient 1.25 times the light) and the level, each to the power the mode
gives it. The engine cannot see a note's release, so a held note's gate holds
until the OP-XY's amp envelope ends it.

### Level

A full-scale fold with the gate open sounds at 0.105 of full scale. The
half-band's phase and the offset's removal can raise a folded wave's peak by
up to 1.86 times, and a strike adds up to 1.6 dB, so a voice stays within
0.22 of full scale (the worst of 1,500 random settings and notes) under the
declared 0.27 (`outputPeakQ31`), which a clamp guarantees and never reaches.

## Screen

A moth, wings spread, drawn in line, with the specimen label pinned in the
corner. Each wing is its edge: the folded waveform, one cycle wrapped along
the margin, the forewing its first half and the hindwing its second. Each
half-cycle swells a lobe, and every fold splits it into scallops. The right
wing shows the wave half a cycle on and turned over, so bias, or FM at an odd
ratio, makes the wings differ. FM's modulator is drawn as a strobe at the
note's own rate: the nearest half-harmonic stands still and what is left over
crawls along the edge (1.01 slowly, 2.76 quickly) while the moth sounds. As
the index rises it frays the edge, drawn through all 62 margin points (the
board drew 60).

The body is the vactrol: the LED's leads bent up as antennae, the LED its
head, shining down onto the photocell, whose serpentine track makes the
abdomen. Each held note has its own vactrol, with the laws audio uses: its
LED flashes as the note is struck, and its cell lights its own eyespot (odd
voices on the left wing, even on the right) and, with the others, the wings.
Inside each edge a lit edge grows toward it and brightens with the light,
then fades at glow's pace once the note ends. The lit edge shows the gate's
waveform: vca keeps its scallops as the light shrinks, lp melts them back
toward a smooth wing. A strike flicks the antennae out and back within 0.2 s.
It also knocks up to two scales off the wings' edges, faster the harder it
pings; they tumble out and settle as they fade. Drift makes the edges wander
and sways the whole moth, up to 2.9 degrees and 2.2 px at full drift, while
it sounds. Fine static veins run from the root toward the margin.

Controls ease in over about 0.15 s and the ratio glides between stops. The
screen cannot hear the voices: it follows the held notes (a note started and
released between frames still pings the next vactrol) and the controls. When
nothing sounds the screen is still. Reduced motion, a reset and a track change
show the end state at once.

### Redrawing little

The OP-XY redraws whatever changes, so the screen changes little:

- **The wings are outlines.** There is no membrane fill. A wing is two
  64-point outlines (the edge and the lit edge), re-keyed only when their
  points change. The veins and eyespots stay put on the wing: the veins end
  at 0.76 of the margin's reach and the eyespots sit nearer the root, inside
  the deepest scallop (0.78), so no fold brings the edge in past them. The
  lit edge stays at least 3% inside the edge, even where lp's melting would
  carry a dip's light past it.
- **The wave and the sway move in steps** of an eighth of a second (the
  clock, drift's wander and the FM crawl are sampled then). The sway moves in
  whole pixels and steps of 0.4 degrees, and only when it has moved three
  quarters of a step past where it was drawn. Each step reaches one group of
  pieces a frame: the left wing's edges and veins, the right wing's, the LED
  (antennae to the light crossing), the cell, then each wing's eyespots. A
  frame therefore moves at most 9 pieces for the sway. The sway waits out
  the 0.2 s after a strike, when the LED and antennae are busy.
- **The light is stepped.** The gate's light (the lit edges' size and grey,
  and the cell) has sixteen steps, the LED eight and each eyespot eight, each
  redrawn only when it steps. The antennae flick in 2 degree steps. The
  scales are drawn at whole pixels and in 30 degree turns.

Measured with the preview's real frames, a note every half second held for a
quarter second (`mothcost.mjs`), at drift 50 then 100:

| | nodes changed a frame, average | peak | outlines re-keyed a frame | host µs a frame |
| --- | --- | --- | --- | --- |
| Ping | 4 / 4 | 15 / 17 | 0.4 / 0.4 | 8 |
| Lantern | 3 / 4 | 13 / 13 | 1.2 / 1.2 | 6 |
| Clean | 3 / 4 | 13 / 15 | 1.5 / 1.5 | 4 |

The build before this one changed 26, 38 and 33 nodes a frame on average
(peaks 120, 103 and 77) and re-keyed 2.2, 4.0 and 3.6 outlines. With two
notes held under a pad at full drift, about 3 nodes change a frame (peak 9)
and 0.5 outlines are re-keyed. The tests check that a held, swaying pad never
changes more than 16 nodes in a frame and averages under six.

`tools/make_panel.py` writes `panel.svg.fragment` and `wing.h` (the wing's
margin and the eyespots' places) with the node numbers `display.h` names. The
screen uses 64 nodes (14 veins, 8 eyespots of two nodes, 14 body parts and 8
scales among them), 4 outlines of 64 points, and all 16 text slots (the
two-page header).

Departures from the board:

- **The wings are outlines,** not filled: the edge, and the lit edge growing
  and brightening inside it. The eyespot is a ring and a pupil.
- **No wing beat.** The board narrowed the wings at each strike. Here a
  strike flicks the antennae instead, beside the LED's flash, the lit edges,
  the eyespot and the shed scales.
- **No halo.** The board's lamp lit rings of glow behind the moth. As filled
  discs they redrew most of the screen at every step of the light. As thin
  rings they would still redraw a box around most of the moth each time the
  light stepped, so they are left out.
- **The sway steps** (above), and reaches the moth's pieces over a few frames
  rather than all at once.
- **The veins and eyespots stay put** on the wing, rather than following the
  edge, and the veins stop short of the margin.
- **Fewer scales:** up to two a strike, from a pool of 8 (the board shed up
  to nine and kept 64).
- **Glow is honest:** the lit edges fade by the engine's vactrol, where glow
  is the time to fall 60 dB. The board's sketch used it as a time constant,
  so for the same caption the wings now fade several times sooner, with the
  sound.
- The label's text is upright (authored SVG has no italics).
- Drift's caption follows its new law: 3.9c at 25 (the board showed 1.6c for
  25 x²), so a quarter turn is clearly heard.
- Clean holds the gate open (strike 0) in combo, where the board's sketch had
  strike 10 in lp, so the init sound swells in and glow and strike are heard
  from the first turn.

## For the screen

`laws.h` is deterministic and has no audio state, so the display includes it:

- `drive_q16`, `index_q16`, `ratio_stop` and `kRatioQ16`, `glow_log2`,
  `drift_cents_q16`, `drift_fold_q15`, `wander_rate`: the controls' laws
- `fold_plan` and `folded`: the folder, as audio runs it
- `vactrol`, `gate_step`, `mode_mix` and `lit`: the vactrol and the mode
- the caption writers: `index_caption`, `ratio_caption`, `glow_caption`,
  `mode_caption`, `drift_caption`

Controls are 0..32767; use `control(q31)` to convert a Q31 value.

## Files

- `engine.cpp`, `core.h`: the voice.
- `laws.h`, `sine.h`: the controls' laws, the vactrol and the sine tables,
  shared by audio and the screen.
- `display.cpp`, `display.h`, `wing.h`: the screen.
- `panel.svg.fragment`: the screen's artwork, written with `wing.h` by
  `tools/make_panel.py`. Run the script after changing a shape, and commit
  all three.
- `tests/sound.cpp`, `tests/screen.cpp`: the sound and screen checks, run by
  `sdk.py test`.

## CPU

The host estimate (`sdk.py test`) is about 7,000 to 11,000 Blackfin
instructions per voice block at the heaviest setting (it varies from run to
run). By static count of the pinned Blackfin compiler's output, every sample
loop is a zero-overhead hardware loop with no stack traffic: the modulator,
carrier and folder take 15, 15 and 17 instructions a sample at twice the rate
(6,016 a block), the half-band's two paths 13 each (1,664), the decimator 8
(512), the gate's two poles 9 (576) and the fade 7 (448): 9,216 a block, plus
the once-a-block planning, 2,319 instructions of code (most of it on branches
a block does not take) and a few divisions, so about 10,300 a block and at
most about 12,000. The loops use
32-bit integer products, one instruction each on the Blackfin and on a host,
except the gate's two poles and the fade, which keep Q31's precision. These
are estimates, not device measurements.
