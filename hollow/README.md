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
| breath | 0–100 | 35 | amount | Bow to breath. At 0 the bow holds steady, pure tones (with a little bow hiss). At 100 noise alone drives the modes. The two cross at about equal power. Breath also widens the resonances, by up to 16× at 100 (by Breath squared), so the breath rings in wide, breathy bands rather than thin whistles; the bow's tone stays pure and only blooms sooner. |
| body | vox, str, pipe, glass | vox | family, or `vo/st`-style between two | Resonator family, morphing continuously from one to the next. vox: harmonics 1–7 and 9 shaped by the vowel's formants. str: harmonics 1–8, bright and slow to fade. pipe: a stopped pipe's odd harmonics. glass: inharmonic bowl partials in beating pairs. |
| bright | 0–100 | 45 | amount | The bow's harmonics fall from 12 dB per octave (closed, nearly a sine) through flat to rising 1.5 dB per octave (open). The breath's low-pass opens from the note itself to 256× it. Loudness stays even (the output gain holds it within ±12 dB). |
| ring | 0.04–40 s (before the body's scale) | 1.0 s | ring time of the fundamental, scaled by the body (vox 0.7×, pipe 0.85×, glass 1.5×), e.g. `28ms`, `0.4s`, `1.0s`, `60s` | How long the modes ring: the bloom when a note starts (at once at 0; about 9 s for vox at 100) and how narrow and pure the breath's tones are (wide noisy bands at 0, slowly drifting pure tones at 100). Higher modes fade sooner by the body's tilt. |
| air | 0–100 | 0 | amount | Unresonated breath hiss (about 450 Hz to 5 kHz) mixed in, rising evenly with the control: clearly there by 25, about as loud as the voice at 100. |
| vowel | oo, oh, ah, eh, ee | ah | nearest vowel | The formant envelope over absolute frequency. It shapes the vox body with its formants deepened 1.3× (troughs bottom out 24 dB down) and colours the other bodies strongly too (str 75%, pipe 85%, glass 55%). |
| spread | 0–30 cents | 0 | cents, e.g. `10c` | An ensemble. Every mode also sounds as a sharp and a flat copy, each detuned by the shown cents either side (4c at 25, 10c at 50, 19c at 75, 30c at 100) and swaying about it, ±35% at about 0.23–0.31 Hz and ±8% at 4.3–5.7 Hz, at each voice's own phase and rate. The copies come in as Spread rises and reach 0.3 each beside the note's 0.4 from 30 up, so the note itself stays the strongest voice. Loudness stays even. |
| sympathy | 0–100 | 0 | amount | How strongly this voice rings at other held notes' pitches, from nothing to within about 7 dB of the note's own fundamental at 100. The breath excites those modes, up to twice as hard at 100. The bow excites them only where one of this note's harmonics falls within their resonance, so octaves, fifths and twelfths bloom most. They ring longer than the note (up to 2.8× at 100), so they bloom in slowly. The output gain makes room for them as they bloom, so a chord grows fuller rather than louder. |

Air, spread and sympathy colour the sound, so they default to off, and at 0
they leave it exactly as it was. Sympathy is the machine's signature: every
preset turns it up. The presets: Choir (the defaults, sympathy up: clean),
Dusk, Bowed glass, Bottle, Strings (a string section with the ensemble),
Ensemble (Spread at 30 cents) and Whisper (breath alone, short and airy).

Stereo: Spread would naturally pan its copies apart, but SDK synth voices
render one mono output (stereo output is only for sample engines), so the
ensemble is mono here. Width would need SDK support for stereo synth voices;
until then, the OP-XY's own send chorus adds it.

Older sounds: Ring's range grew from 0.1–20 s to 0.04–40 s and Spread
changed from a 15-cent detune to the ensemble, so a sound saved with an
earlier Hollow keeps its Ring position but rings a little shorter below
the middle and longer above (0.5 is now 0.9 s for vox, was 1.0 s; set 0.52),
and its Spread sounds as the ensemble.

## How it works

Each mode is a damped resonator solved in phasor form. Once per 64-sample
block, its complex amplitude decays by the mode's ring time, moves toward the
bow's steady push, and is jostled by the breath's noise. This is exactly how a
resonator's envelope evolves when it is driven near its own frequency. A sine
oscillator then plays that amplitude, ramped across the block. The per-block
decay factor is below 1 at every setting, so no mode can grow at any Ring.
The per-sample cost is one table read and two 16×16 multiplies per mode. All
exponentials, damping and gains are planned once per block.

### Spread's ensemble

A unison of three players gives each partial three copies a few cents apart,
beating against each other and drifting as the players do. In phasor form a
copy is the mode itself with a phase that runs ahead (sharp) or behind (flat)
at the copy's detune. So each mode keeps two extra phases, advanced once a
block by its own increment times the copy's detune, and the sum of the three
copies is the mode's complex amplitude turned and scaled by
`centre + side (e^iu + e^id)`: two table reads per copy and eight multiplies a
block per mode. The sample loop does not change, so the ensemble costs almost
nothing. The copies' detunes sway with two slow and two quick sines of each
voice's own clock, at rates up to 12% apart between voices, so a chord's
notes never move together.

This is a true ensemble rather than a static detune of some partials: the
earlier Spread flattened half the modes, which made the partials slightly
inharmonic but gave no partial a second copy to beat against, so it barely
moved. With the ensemble, a held bowed note's level moves by about 25%
(6% without) and its fourth harmonic spreads to about 22 cents either side
at 100.

A copy's phase moves at most an eighth of a cycle per block, so the block's
linear ramp still follows the beating at the top of the range. Because the
copies beat, they add in power: the output gain allows for that, so Spread
leaves the level even.

### Level

The output gain holds the voice's steady power, its sympathetic modes
included (as far as they have bloomed), so bodies, vowels, Bright, Spread,
Sympathy and the keyboard's range all sound about equally loud (the gain
stays within −12 dB to +12 dB). It ramps across each block, so a change of
gain never steps. A soft limit keeps every sample within half scale. The
output sits low enough that eight notes keep headroom: a full chord of the
presets peaks at 0.53–0.65 of full scale, and at most 0.94 with Breath, Air,
Bright, Ring, Spread and Sympathy all at 100.

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

An engraved plate of acoustics, in the manner of Helmholtz and Tyndall.

Fig. 1 is whichever body Body is nearest, drawn in section to real proportion:

- vox: the organs of voice, cut down the middle: the nasal cavity and its
  conchae, the palate and raised velum, the tongue, the pharynx, the larynx
  with its folds, and the cervical spine behind. The airways are cut out of
  the tissue in black.
- str: a violin from the front (593 mm long at 0.47 px/mm), with its
  purfling, arching, f-holes, bridge, strings, pegs and scroll, and the bow
  across the strings.
- pipe: a stopped wooden pipe in section (walls hatched as cut wood, the
  block, cap, flue, mouth and leathered stopper), the same pipe from the
  front, and a smaller pipe a fifth above, on the wind chest.
- glass: two wine glasses with a little water, the second smaller.

Inside it the body's standing wave is drawn as one bright line: along the
vocal tract from glottis to lips, along the D string from nut to bridge,
up the pipe's air column from mouth to stopper, or round the glass's rim.
It is the sum of the engine's eight modes, each in its own shape (the
string's harmonics, the stopped pipe's odd quarter-waves, the rim's bending
modes with two, three, four, five and six waves round it) and each as strong
as the engine makes it for an A3: the body's level coloured by the vowel's
formants (`mode_level`), tilted by Bright and shared between the bow and
the breath as `engine.cpp` drives them. Each mode turns at a slowed rate in
its true ratio, so the picture is a stroboscope of the sound: a bright,
jagged wave with Bright up, a smooth one with it down, a different shape for
each vowel, the glass's beating pairs slowly turning its rim.

Fig. 2 is Koenig's analyser: a Helmholtz resonator for each mode, hung from
its capsule under a rail of burners. Each resonator is as large as its
mode's pitch needs (its radius by the ratio to the -2/3, as for a fixed
neck), so Body visibly retunes the set: harmonic for vox and str, the odd
harmonics' fast-shrinking row for pipe, matched pairs for glass. Each
flame is solid, grey with a white core, and burns as high as its mode rings,
a pilot flame when it is still, read
against a rod graduated every 10 px on the left post.

What each control does:

| Control | On screen |
| --- | --- |
| breath | The bow's steady drive gives way to the breath's noise: each mode's part of the wave wanders, faster as the resonances widen; motes of breath leave the lips, the bow, the pipe's mouth or the rim (none to 24, by the breath's share); the flames flicker; the pipes' wind sheets flutter; and the bow on the violin dims once its share falls below half (Breath above about 70). |
| body | Which body is drawn; the modes' shapes and rates; the resonators' sizes (between families they morph with the engine's ratios). |
| bright | The modes' levels by the bow's slope and the breath's low-pass: a smooth wave and one tall flame closed, a jagged wave and a row of flames open. |
| ring | How fast each mode blooms while a note is held and how long it rings out after (each mode by its own ring time, high modes first, from `ring_log2_seconds` and the body's fade), and how slowly the breath's part wanders. |
| air | Streaks of hiss spraying from the same place, faster than the breath (none to 16). |
| vowel | The formants: the levels of every body's modes, so the wave's shape and the flames. On vox the tongue moves (high and back for oo, low for ah, high and front for ee), the lips round or part, and the tract over the tongue follows it. |
| spread | The ensemble: two fainter copies of the wave either side (1 px out and 4 px at 30 cents on the string and pipe, closer in the vocal tract, and only a hair off the glass's rim, where the wave itself stays on the rim), each beating ahead of or behind the note at a rate set by the detune and swaying at the engine's slow rates. |
| sympathy | The partner (the nasal cavity, the open G string, the neighbouring pipe's air column or the second glass's rim) shows as soon as Sympathy couples it, at rest or with one note: leaning toward the body as far as it's coupled, brighter from 50. With two or more notes held it rings, blooming slowly and ringing up to 2.8 times longer, louder with Sympathy and with more notes. |

Notes: the screen cannot hear the voices, so the modes bloom while any
note is held and ring out once all are released; a note played and released
between frames still rings briefly. The clock runs only while something
rings, slowing as it fades, so the screen is still when nothing sounds.
Reduced motion, a reset and a track change show the end state at once (a
still picture of the wave). Controls ease within about 0.15 s.

`tools/make_panel.py` draws the artwork from the bodies' real proportions
and writes `panel.svg.fragment` (about 60 KB) and the generated block of
`display.h` (node numbers, the strands' rest lines and the modes' shapes).
The frame uses 4 outlines of 64 points (the wave, the partner and the
ensemble's two copies), 209 nodes including the header's 8, all 16 text
slots for the two-page header, and 16 palette entries. Figure nodes keep
their authored paint; the palette shades the strands, flames, resonators,
motes, streaks and bow. The generator checks every path against the device's
rules (`check()`): one subpath, at most 64 points, neighbours 1/8 px apart,
a closing edge of at least 1/4 px and no turn straight back.

The screen spends its redraws in turns, so the frame's cost is bounded however
hard the controls are set (`tests/churn.cpp`). Each part is only looked at on
its turn: four flames and their cores a frame, six motes, four streaks, the wave
and partner on one frame and the copies on the next, each reloading only once a
point has moved 1/4 px. Flames step about a pixel at a time, and the bow, tongue
and jets in whole pixels or small turns. The flames are solid on purpose, and
the bow, tongue, resonators, motes and streaks keep their fills too. A change of
body takes the old figure down and draws the new one up, 16 nodes a frame.

About 6 nodes change a frame at the defaults, and at the heaviest settings 17.5
on average with a peak of 23. Turning a control peaks at 29, and nothing changes
at rest. `tests/churn.cpp` holds a playing frame to 20 on average with a peak of
24.

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
- `breath_widen_log2(breath)`: how much Breath widens the resonances
- `spread_cents_q8(spread)` and `spread_wet(spread)`: the ensemble's detune
  and the copies' share; `kSwaySlow`, `kSwayQuick` and `kSwayRates`: its sway
- the caption writers: `body_caption`, `ring_caption`, `vowel_caption`,
  `spread_caption`

Controls are 0..32767; use `control(q31)` to convert a Q31 value.

## CPU

Measured on an OP-XY (OS 1.2.1) with the SDK's `profile benchmark`, at
`v1.3.0`, as one capture of its sweep: one voice playing middle C with the
first four controls at their midpoint costs **4.09%** of the 580,352-cycle
audio block, about 23,800 cycles, so eight voices take about 33%. The heaviest
setting in the sweep, Air at 100%, costs **4.53%**, so eight voices there take
about 36%. The OP-XY's own engines measure 1.6–2.7% a voice at the midpoint.

The host estimate (`sdk.py test`) is about 7,800 Blackfin instructions per
voice block at the heaviest setting (one note, so no sympathetic modes; the
same machine measured 7,600 before the ensemble). Each mode's sample loop
compiles to 17 Blackfin instructions in a zero-overhead hardware loop, so
twelve modes (eight body modes and four sympathetic ones) cost about 13,000
instructions a block before the per-block planning. Spread's ensemble adds
only per-block work, a few dozen instructions a mode.
