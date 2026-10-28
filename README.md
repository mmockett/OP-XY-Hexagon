# OP-XY Hexagon

Synths for the OP-XY, built with the
[Open-XY SDK](https://github.com/etcetc000/open-xy), for pads and leads. Three
are here so far, of six: a polysynth heard through worn tape, an additive voice
whose partials drift on their own, and a choir of tuned resonators that ring
into each other. Each one draws its own screen.

**Status: experimental.** All three pass the SDK's tests and compile for the
OP-XY. They have been played on the device, but their CPU cost has only been
estimated on a computer.

## The synths

<table>
<tr>
<td width="50%"><img src="docs/screens/wane.gif" width="480" alt="Wane's screen: a C60 cassette whose reels turn and whose tape wobbles past the head as four notes start, needles lighting one by one"></td>
<td>

### Wane

**Tape-worn analogue polysynth.** Two detuned, morphing oscillators per note
(sine through triangle and saw to a narrow pulse), a sub and a breath of
noise, all played through one shared, worn cassette: a chord wows, flutters,
drags and sags together, and age dulls, saturates and hisses.

**Page 1:** shape · detune · drift · warmth<br>
**Page 2:** wow · flutter · sag · age

The screen is a C60 drawn to real proportions. The reels turn and the tape
past the head moves with the same wow and flutter the audio plays; a needle
lights for each note and kicks back with sag. *Above: Attic, four notes
joining, then wow swept up.* [More about Wane](wane/README.md)

</td>
</tr>
<tr>
<td width="50%"><img src="docs/screens/bloom.gif" width="480" alt="Bloom's screen: a meadow of partials under a low sun, blossoms opening as a chord starts, formant hills sliding across as the vowel sweeps, then twin blossoms parting as haze rises"></td>
<td>

### Bloom

**Breathing additive pads.** Twenty-four sine partials, shaped like an
analogue wave and sung through vowel formants, that drift, smear and beat on
their own: from a clean saw through PWM strings and choirs to glittering
bells and breathy fog.

**Page 1:** tilt · shape · stretch · vowel<br>
**Page 2:** breath · blur · haze · time

The screen is a meadow of partials drawn from the engine's own levels and
pitches, the vowel's formants as hills behind it and haze's detuned pairs as
twin blossoms. *Above: Tape Choir, a chord held while the vowel sweeps oo to
ee and haze rises.* [More about Bloom](bloom/README.md)

</td>
</tr>
<tr>
<td width="50%"><img src="docs/screens/hollow.gif" width="480" alt="Hollow's screen: a shelf of eight vessels, four lighting as notes start with ripples rising and arcs joining them, the vessels morphing from urns through string bodies and organ pipes to wine glasses as body sweeps"></td>
<td>

### Hollow

**Resonator choir.** Breath and a soft bow drive tuned resonant bodies that
morph from a sung vowel through a string and a stopped pipe to glass. Held
notes ring sympathetically in one another, and spread turns every resonance
into a swaying ensemble.

**Page 1:** breath · body · bright · ring<br>
**Page 2:** air · vowel · spread · sympathy

The screen is a shelf of eight vessels in the body's shape. Each note lights
one, which rings out for the ring time shown; ripples rise in the vowel's
shape, spread doubles them and arcs join notes as sympathy rises. *Above:
Ensemble, four notes joining, then body swept from voice to glass.*
[More about Hollow](hollow/README.md)

</td>
</tr>
</table>

## Playing them

Choose Wane, Bloom or Hollow on an instrument track. Each has eight controls on
two pages: press any encoder to switch pages, and Shift + turn makes fine
steps. The controls that colour the sound (Wane's tape, Bloom's breath,
Hollow's sympathy) start at zero; each machine's presets, in its
`definition.json`, turn them up. The OP-XY's own
envelopes, LFOs, filter and effects apply as for any instrument.

All the notes of one machine share one state across tracks, so Wane's tape,
Bloom's weather and Hollow's sympathy also join notes on different tracks that
use the same machine.

Each voice sits low enough that a chord of eight notes keeps headroom: every
preset's eight-note chord peaks between 0.4 and 0.8 of full scale, before the
OP-XY's own envelope, filter and effects.

Spread on Hollow is a chorus within each voice. Synth voices are mono in the
Open-XY SDK, so it can't pan voices apart; the OP-XY's own chorus on a send
adds width.

## Building

The machines need SDK 1.4 (the two-page header), on the `staging` branch of
[etcetc000/open-xy](https://github.com/etcetc000/open-xy). Check it out beside
this repository as `../open-xy`, fetch its toolchain
(`python sdk.py toolchain fetch`), then:

```sh
python ../open-xy/sdk.py test --machine wane --machine bloom --machine hollow
python ../open-xy/sdk.py build --machine wane --machine bloom --machine hollow --stock-os <your stock-1.2.1>/os.ldr --layout <your last build's layout.json> --out <folder outside this checkout>
```

Then install the build's `candidate/` files as the SDK's
[build guide](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#5-install)
describes. Pass your last build's `layout.json` so the machines already on your
OP-XY keep their numbers.

They ask for these numbers; a build gives them out when they're free:

| | Engine | Page |
| --- | --- | --- |
| Wane | 59 | 107 |
| Bloom | 60 | 108 |
| Hollow | 61 | 109 |

## Develop

Each machine's folder holds its engine (`engine.cpp` and its headers), its
screen (`display.cpp`, with the artwork `panel.svg.fragment` written by
`tools/make_panel.py`) and its sound tests (`tests/sound.cpp`, run by
`sdk.py test`). The animations in `docs/screens` are the machines' real
display code, frame by frame, run through the SDK's browser preview along a
scripted performance, with Helvetica Neue standing in for the OP-XY's Univers.

## Licence

MIT ([LICENSE](LICENSE)).
