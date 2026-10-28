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

A shelf of eight vessels in the body's shape: an urn (vox), a tall string
body with its peg (str), an organ pipe with its mouth (pipe) or a wine glass
(glass), whichever family Body is nearest. Each held note lights one vessel,
left to right. A lit vessel swells as its note blooms, more slowly with a
longer ring and sooner with more breath (which widens the resonances), and
glows brighter as it fills. Once released, it
rings out over Ring's time, as the engine maps it (`ring_log2_seconds`).

From each ringing mouth:

- Ripples rise and fade. They are slower and wider the longer the ring, and
  brighter with Bright. Vowel sets their shape, round for oo and flat for ee.
  With Spread, the ensemble's sharp and flat copies rise as fainter ripples to
  the right and left, from 1.5 px out up to 7.6 px at 30 cents, each swaying
  with its copy's detune at the engine's slow rates.
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
chord with Spread up could show 72, so the faintest are left out.

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

The host estimate (`sdk.py test`) is about 7,800 Blackfin instructions per
voice block at the heaviest setting (one note, so no sympathetic modes; the
same machine measured 7,600 before the ensemble). Each mode's sample loop
compiles to 17 Blackfin instructions in a zero-overhead hardware loop, so
twelve modes (eight body modes and four sympathetic ones) cost about 13,000
instructions a block before the per-block planning. Spread's ensemble adds
only per-block work, a few dozen instructions a mode. These are estimates,
not device measurements.
