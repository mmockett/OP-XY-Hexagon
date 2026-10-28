# Wane

An analogue-style polysynth heard through one worn cassette. Each voice has
two oscillators that morph from sine through triangle and saw to pulse, a triangle sub
an octave down and a breath of noise. Every voice plays through the same
tape, so a chord wows, flutters, drags and sags together, as a real cassette
does. Age wears the tape: duller highs, a harder saturation and hiss.

With the colouring controls at zero (drift, wow, flutter, sag, age) Wane is a
clean, steady pad: two saws 8 cents apart through a gentle low-pass. The
OP-XY's own envelopes, filter, LFOs and effects do the rest; the init preset
has a slow attack and long release.

## Controls

Press any encoder to switch pages. Each control is plainly heard by a quarter
turn and drastic at the top: the pitch controls follow curves through the
values below at 0, 25, 50, 75 and 100 (definition.json's anchors), straight
between them.

| Page | Control | Range (0 / 25 / 50 / 75 / 100) | Default | Header | What it does |
| --- | --- | --- | --- | --- | --- |
| 1 | shape | sine, triangle, saw, square, pulse | saw (50) | `sine`, `s/tri`, `tri`, `t/saw`, `saw`, `s/sq`, `sq`, `pulse` | Both oscillators: sine to triangle to 25, triangle to saw to 50, saw to square to 80, then the square narrows to a 15% pulse. Band-limited (PolyBLEP). |
| 1 | detune | 0 / 3.5 / 11 / 30 / 70 cents | 8 cents (40) | cents | How far apart the two oscillators sit, either side of the note: at 220 Hz they beat 0.4, 1.4, 3.8 and 9 times a second. |
| 1 | drift | 0 / 5 / 12 / 22 / ±35 cents | 0 | cents | Each note starts a random distance off pitch and each voice wanders slowly on its own; oscillator 2 wanders a little apart. At the top a chord is far out of tune with itself. |
| 1 | warmth | 0 to 100 | 20 | amount | A darker two-pole low-pass, from 17.8 kHz to 5.1, 2.1, 0.9 and 0.37 kHz at 220 Hz, following the keyboard by up to half as it closes; more of the sub and breath, a harder push into the soft saturation. |
| 2 | wow | 0 / 16 / 42 / 78 / ±120 cents | 0 | cents | The tape's slow, irregular speed error (about 0.3 to 1.5 Hz), and now and then a drag that pulls everything down by 1.25 times as much (150 cents flat at the top). |
| 2 | flutter | 0 / 8 / 20 / 34 / ±50 cents | 0 | cents | Faster wobble, 7 to 11 Hz, its depth breathing. |
| 2 | sag | 0 / 30 / 70 / 120 / 180 cents | 0 | cents | As each note starts the tape catches up: every sounding voice, held notes too, dips flat by this much within 23 ms and recovers, halving every 0.17 s at 25 and every 0.35 s at the top, so it settles in 0.6 s at 25 and 2 s at 100. |
| 2 | age | 0 to 100 | 0 | amount | The tape head's high-frequency loss (two poles, 4.8, 2.6, 1.7 and 1.1 kHz at 25, 50, 75 and 100), its worn contact dulling it further now and then (up to an octave), a harder saturation and hiss. Never a dropout in level. |

Warmth was 35 by default and darkened less; 20 now sounds as 35 did. The
other controls' curves changed too, so sounds saved with earlier versions
play wider wow, flutter, sag, drift and detune than they did.

## Screen

A compact cassette fills the middle of the page, drawn to real proportions
(100.4 × 63.8 mm), with the oscillator on the left and a needle for each held
note on the right. It is still until you play or turn something.

- **osc** shows two cycles of the oscillator: shape morphs it from sine
  through triangle, saw and square to a narrow pulse, and warmth rounds it off. Detune
  draws a dim twin behind it, which slides along while notes are held, faster
  the wider the detune.
- **notes** has eight needles. Each held note lights one; drift makes the lit
  needles sway slowly, each at its own pace, as far as drift's depth, and sag
  kicks a needle back by its depth as its note starts, easing back as the
  tape recovers.
- **The cassette** runs while notes are held: the reels turn, and the tape
  across the foot carries the tape's recent speed past the head, the newest at
  the left, at 0.9 mm a unit of 60 cents. Wow bends it slowly, with the odd
  drag, flutter adds a fast ripple and each note start dips it by sag. It
  stays between the capstans and the shell's edge. The reels turn with the same wobble, and
  over a long session the tape winds from one pack onto the other.
- **Age** wears the cassette, and the wear stays still: what age shows is a
  used object, not a motion. From the first steps dust settles and the label's
  top-right corner starts to lift; by 30 a first scratch crosses the window
  and another the shell. Further up the label's stripes fade (in three steps,
  at 34 and 67), more scratches cross the window, the shell and the foot, its
  edges rub pale and chip, the print wears out of "wane", the side letter and
  the title lines, and grime gathers in corners and along edges. The corner
  peels further in four stages, its pale underside curling back over the
  label to leave bare shell; from 66 the bottom-left corner lifts too, and
  from 78 the label tears. At 100 it is a well-used tape. Each piece appears at
  once as age passes its point, and nothing moves.

The artwork is generated from the cassette's geometry by
`tools/make_panel.py`; run it after changing a shape, and keep the constants
it prints with `--tables` in step with `display.cpp`.

## For the screen

The tape's motion is a pure function of a block clock, in `tape.h`, which a
display can include: `tape::Clock` turns frame time into audio blocks, and
`tape::bend(clock, tape::depths(wow, flutter, sag))` gives the transport's
pitch in Q16 octaves, as the audio plays it (`tape::wow`, `flutter` and
`stretch` give each motion alone, Q15). The start sag is
`tape::sag_bend(blocks_since_start, from, depths)`, its recovery
`tape::sag(blocks_since_start, from, depths.sag_rate)`. The screen's clock is its
own, so it draws the same tape, not the same moment of it. `controls.h` holds
the control slots and depths the captions use.

## CPU

Measured on an OP-XY (OS 1.2.1) with the SDK's `profile benchmark`, three
runs: one voice playing middle C with the first four controls at their
midpoint costs **4.11%** of the 580,352-cycle audio block, about
23,800 cycles, so eight voices take about 33%. The OP-XY's own engines
measure 1.6–2.7% a voice in the same test. The test plays one setting;
heavier settings cost more.

The SDK's host estimate (`sdk.py test`) is about 7,000 to 10,000 Blackfin
instructions per voice block at the heaviest setting. Each voice runs its
oscillators and filters with 16 × 16 multiplies; the tape is computed once per
block for all voices.
