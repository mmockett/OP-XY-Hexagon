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

Press any encoder to switch pages. The pitch controls are squared, so the
first part of each turn is fine.

| Page | Control | Range | Default | Header | What it does |
| --- | --- | --- | --- | --- | --- |
| 1 | shape | sine, triangle, saw, square, pulse | saw (50) | `sine`, `s/tri`, `tri`, `t/saw`, `saw`, `s/sq`, `sq`, `pulse` | Both oscillators: sine to triangle to 25, triangle to saw to 50, saw to square to 80, then the square narrows to a 15% pulse. Band-limited (PolyBLEP). |
| 1 | detune | 0 to 50 cents | 8 cents | cents | How far apart the two oscillators sit, either side of the note. |
| 1 | drift | 0 to ±20 cents | 0 | cents | Each note starts a random distance off pitch and each voice wanders slowly on its own; oscillator 2 wanders a little apart. |
| 1 | warmth | 0 to 100 | 35 | amount | A darker two-pole low-pass (17.8 kHz to 0.9 kHz), more of the sub and breath, a harder push into the soft saturation. |
| 2 | wow | 0 to ±40 cents | 0 | cents | The tape's slow, irregular speed error (about 0.3 to 1.5 Hz), and now and then a drag that pulls everything up to 60 cents flat. |
| 2 | flutter | 0 to ±12 cents | 0 | cents | Faster, shallower wobble, 7 to 11 Hz, its depth breathing. |
| 2 | sag | 0 to 45 cents | 0 | cents | As each note starts the tape catches up: every sounding voice dips flat by this much and recovers within half a second. |
| 2 | age | 0 to 100 | 0 | amount | The tape head's high-frequency loss (down to 2.6 kHz), saturation and hiss. |

## Screen

A compact cassette fills the middle of the page, drawn to real proportions
(100.4 × 63.8 mm), with the oscillator on the left and a needle for each held
note on the right. It is still until you play or turn something.

- **osc** shows two cycles of the oscillator: shape morphs it from sine
  through triangle, saw and square to a narrow pulse, and warmth rounds it off. Detune
  draws a dim twin behind it, which slides along while notes are held, faster
  the wider the detune.
- **notes** has eight needles. Each held note lights one; drift makes the lit
  needles sway slowly, each at its own pace, and sag kicks a needle back as
  its note starts.
- **The cassette** runs while notes are held: the reels turn, and the tape
  across the foot carries the tape's recent speed past the head, the newest at
  the left. Wow bends it slowly, with the odd drag, flutter adds a fast ripple
  and each note start dips it by sag. The reels turn with the same wobble, and
  over a long session the tape winds from one pack onto the other.
- **Age** fades the label's stripes in three steps and gathers dust on the
  shell, which shivers while notes play.

The artwork is generated from the cassette's geometry by
`tools/make_panel.py`; run it after changing a shape, and keep the constants
it prints with `--tables` in step with `display.cpp`.

## For the screen

The tape's motion is a pure function of a block clock, in `tape.h`, which a
display can include: `tape::Clock` turns frame time into audio blocks, and
`tape::bend(clock, tape::depths(wow, flutter, sag))` gives the transport's
pitch in Q16 octaves, as the audio plays it (`tape::wow`, `flutter` and
`stretch` give each motion alone, Q15). The start sag is
`tape::sag_bend(blocks_since_start, from, depths)`. The screen's clock is its
own, so it draws the same tape, not the same moment of it. `controls.h` holds
the control slots and depths the captions use.

## CPU

About 5,100 Blackfin instructions per voice block on the SDK's host estimate
(`sdk.py test`), and about 6,000 with every control up at once; neither is
measured on a device. Each voice runs its
oscillators and filters with 16 × 16 multiplies; the tape is computed once per
block for all voices.
