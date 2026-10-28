#pragma once
// Hollow's bodies: what each resonator family is made of, as pure functions of
// the controls and the note. Audio and the screen read the same numbers here,
// so a display can draw a body's modes (frequency ratios and levels) without
// any audio state. Nothing in this header changes at run time.
#include <opxy/types.hpp>

namespace hollow {
using opxy::i32;
using opxy::u32;

const u32 kModes = 8;    // tuned resonators per voice
const u32 kBodies = 4;   // vox, string, pipe, glass, in the order Body sweeps them
const u32 kVowels = 5;   // oo, oh, ah, eh, ee, in the order Vowel sweeps them
const u32 kVowelPoints = 129;  // sixteen per octave from 50 Hz to 12.8 kHz

// A Q31 control word as 0..32767.
inline i32 control(i32 word) { return word < 0 ? 0 : word >> 16; }

// (a * b) >> 16 without a 64-bit product: exact while the result fits 32 bits.
inline u32 times(u32 a, u32 b) {
    const u32 ah = a >> 16, al = a & 0xffffu, bh = b >> 16, bl = b & 0xffffu;
    return (ah * bh << 16) + ah * bl + al * bh + (al * bl >> 16);
}

// a + (b - a) * fraction / 32768 for fraction 0..32768, in 32 bits for any
// difference below 2^30: the difference is split at bit 15.
inline i32 mix(i32 a, i32 b, i32 fraction) {
    const i32 d = b - a;
    return a + (d >> 15) * fraction + ((d & 32767) * fraction >> 15);
}

// Index of the highest set bit; value must not be 0.
inline u32 top_bit(u32 value) {
    u32 n = 0;
    if (value >= 1u << 16) { n += 16; value >>= 16; }
    if (value >= 1u << 8) { n += 8; value >>= 8; }
    if (value >= 1u << 4) { n += 4; value >>= 4; }
    if (value >= 1u << 2) { n += 2; value >>= 2; }
    if (value >= 1u << 1) n += 1;
    return n;
}

// Sixteenth-octave tables for log2_q16 and exp2_q16. They sit at file scope
// because the Blackfin GCC defines an inline function's own static table in
// every object that uses it, which the engine and display then both link.
static const i32 log2_table[17] = {0, 5732, 11136, 16248, 21098, 25711, 30109, 34312, 38336,
                                   42196, 45904, 49472, 52911, 56229, 59434, 62534, 65536};
static const u32 exp2_table[17] = {65536, 68438, 71468, 74632, 77936, 81386, 84990, 88752, 92682,
                                   96785, 101070, 105545, 110218, 115098, 120194, 125515, 131072};

// log2(value) in Q16, from a sixteenth-octave table: within 0.0004 octave.
inline i32 log2_q16(u32 value) {
    const i32* const table = log2_table;
    if (!value) return 0;
    const u32 top = top_bit(value);
    const u32 normal = top >= 16 ? value >> (top - 16) : value << (16 - top);  // 65536..131071
    const u32 fraction = normal & 0xffffu, at = fraction >> 12, rest = fraction & 0xfffu;
    return static_cast<i32>(top << 16) + table[at] + ((table[at + 1] - table[at]) * static_cast<i32>(rest) >> 12);
}

// 2^(x / 65536) in Q16 (65536 is 1.0), for x from -16 to +15 octaves.
inline u32 exp2_q16(i32 x) {
    const u32* const table = exp2_table;
    const i32 whole = x >> 16;
    const u32 fraction = static_cast<u32>(x) & 0xffffu, at = fraction >> 12, rest = fraction & 0xfffu;
    const u32 m = table[at] + ((table[at + 1] - table[at]) * rest >> 12);
    if (whole >= 15) return m << 15;
    if (whole >= 0) return m << whole;
    return whole <= -31 ? 0 : m >> -whole;
}

// ---------------------------------------------------------------------------
// The bodies. Mode k of each family morphs into mode k of the next, so Body
// sweeps continuously from a voice through a string and a stopped pipe to glass.
//   vox:    harmonics 1-7 and 9, shaped by the vowel's formants
//   string: harmonics 1-8, bright and slow to fade, as a bow locks them
//   pipe:   a stopped pipe's odd harmonics, hollow and breathy
//   glass:  a bowl's inharmonic partials, each split in a beating pair
// Ratios are Q16 multiples of the note; levels Q15.
const u32 kRatio[kBodies][kModes] = {
    {65536, 131072, 196608, 262144, 327680, 393216, 458752, 589824},
    {65536, 131072, 196608, 262144, 327680, 393216, 458752, 524288},
    {65536, 196608, 327680, 458752, 589824, 720896, 851968, 983040},
    {65536, 65831, 152044, 153027, 278528, 280494, 434504, 614728},
};
// log2 of each ratio (Q16), and log2 of how much sooner the mode fades than
// the fundamental, 1 + tilt (ratio - 1), by its family's tilt (Q16).
const i32 kLog2Ratio[kBodies][kModes] = {
    {0, 65536, 103872, 131072, 152170, 169408, 183983, 207744},
    {0, 65536, 103872, 131072, 152170, 169408, 183983, 196608},
    {0, 103872, 152170, 183983, 207744, 226717, 242512, 256042},
    {0, 425, 79569, 80178, 136804, 137469, 178848, 211654},
};
const i32 kLog2Fade[kBodies][kModes] = {
    {0, 44438, 74548, 97350, 115707, 131073, 144287, 166204},
    {0, 21098, 38336, 52911, 65536, 76672, 86634, 95645},
    {0, 65536, 103872, 131072, 152170, 169408, 183983, 196608},
    {0, 64, 17080, 17257, 37544, 37829, 57882, 76964},
};
const i32 kLevel[kBodies][kModes] = {
    {26000, 23000, 21000, 19000, 17000, 15500, 14000, 12000},
    {24000, 20000, 16500, 14000, 12000, 10500, 9300, 8300},
    {30000, 16500, 11500, 9000, 7200, 6000, 5100, 4500},
    {19000, 19000, 13000, 13000, 9500, 9500, 7000, 5000},
};
// Per family: how much faster high modes fade than the fundamental (Q15: 0
// keeps one decay time for every mode, 1 one Q; kLog2Fade holds its effect), log2 of its ring time over
// Ring's base (Q16), how strongly the vowel shapes it (Q15), and its loudness
// trim (Q12), which evens out the families' levels.
struct Traits { i32 tilt, ring_log2, vowel, trim; };
const Traits kTraits[kBodies] = {
    {19661, -33723, 32767, 2048},
    {8192, 0, 9830, 2700},
    {16384, -15366, 14746, 2900},
    {4915, 38336, 4915, 2900},
};

// Where Body sits: the family below it and the fraction (Q15) toward the next.
struct Place { u32 family; i32 fraction; };
inline Place body_place(i32 body) {
    const i32 at = (body < 0 ? 0 : body > 32767 ? 32767 : body) * static_cast<i32>(kBodies - 1);
    Place p;
    p.family = static_cast<u32>(at >> 15);
    p.fraction = at & 32767;
    if (p.family >= kBodies - 1) { p.family = kBodies - 2; p.fraction = 32768; }
    return p;
}

struct Shape { u32 ratio; i32 log2_ratio, log2_fade, level; };
// Mode k of the body at Body (0..32767), before the vowel and the note.
inline Shape body_mode(i32 body, u32 k) {
    const Place p = body_place(body);
    const u32 f = p.family, g = p.family + 1;
    Shape s;
    s.ratio = static_cast<u32>(mix(static_cast<i32>(kRatio[f][k]), static_cast<i32>(kRatio[g][k]), p.fraction));
    s.log2_ratio = mix(kLog2Ratio[f][k], kLog2Ratio[g][k], p.fraction);
    s.log2_fade = mix(kLog2Fade[f][k], kLog2Fade[g][k], p.fraction);
    s.level = mix(kLevel[f][k], kLevel[g][k], p.fraction);
    return s;
}
inline Traits body_traits(i32 body) {
    const Place p = body_place(body);
    const Traits &a = kTraits[p.family], &b = kTraits[p.family + 1];
    Traits t;
    t.tilt = mix(a.tilt, b.tilt, p.fraction);
    t.ring_log2 = mix(a.ring_log2, b.ring_log2, p.fraction);
    t.vowel = mix(a.vowel, b.vowel, p.fraction);
    t.trim = mix(a.trim, b.trim, p.fraction);
    return t;
}

// ---------------------------------------------------------------------------
// Vowels: a sung vowel's formant envelope over absolute frequency, so the
// same vowel colours every note the way a choir's does. Q12 (4096 is 1.0).
const opxy::u16 kVowelCurves[kVowels][kVowelPoints] = {
    {  // oo
     2864, 2864, 2863, 2862, 2861, 2860, 2859, 2857, 2856, 2853, 2851, 2848, 2844, 2840, 2835, 2829,
     2821, 2843, 2915, 2993, 3079, 3174, 3278, 3394, 3522, 3665, 3824, 4003, 4204, 4431, 4687, 4978,
     5308, 5682, 6107, 6586, 7122, 7713, 8346, 8999, 9628, 10169, 10539, 10658, 10473, 9985, 9253, 8378,
     7461, 6585, 5800, 5129, 4577, 4140, 3809, 3577, 3439, 3396, 3453, 3622, 3919, 4347, 4868, 5340,
     5510, 5189, 4484, 3681, 2981, 2440, 2041, 1750, 1536, 1377, 1257, 1165, 1095, 1040, 998, 966,
     943, 927, 920, 921, 933, 960, 1011, 1100, 1251, 1477, 1677, 1604, 1327, 1094, 950, 864,
     812, 778, 755, 739, 727, 718, 711, 706, 701, 698, 695, 692, 690, 688, 687, 685,
     684, 683, 682, 681, 681, 680, 679, 679, 678, 678, 678, 677, 677, 677, 677, 676,
     676,
    },
    {  // oh
     2864, 2864, 2863, 2862, 2861, 2860, 2859, 2857, 2856, 2853, 2851, 2848, 2844, 2840, 2835, 2829,
     2821, 2813, 2803, 2791, 2777, 2760, 2741, 2718, 2692, 2661, 2626, 2584, 2537, 2483, 2445, 2531,
     2627, 2735, 2859, 2999, 3161, 3348, 3565, 3820, 4120, 4476, 4902, 5412, 6023, 6754, 7612, 8586,
     9622, 10590, 11282, 11468, 11038, 10096, 8901, 7716, 6700, 5922, 5390, 5096, 5034, 5210, 5631, 6277,
     7010, 7484, 7280, 6368, 5174, 4090, 3251, 2641, 2204, 1889, 1658, 1487, 1358, 1260, 1185, 1128,
     1085, 1054, 1035, 1026, 1030, 1050, 1092, 1168, 1303, 1535, 1884, 2174, 2013, 1587, 1260, 1066,
     953, 884, 840, 810, 789, 774, 762, 753, 746, 740, 736, 732, 728, 726, 723, 721,
     720, 718, 717, 716, 715, 714, 713, 713, 712, 711, 711, 710, 710, 710, 709, 709,
     709,
    },
    {  // ah
     2864, 2864, 2863, 2862, 2861, 2860, 2859, 2857, 2856, 2853, 2851, 2848, 2844, 2840, 2835, 2829,
     2821, 2813, 2803, 2791, 2777, 2760, 2741, 2718, 2692, 2661, 2626, 2584, 2537, 2483, 2421, 2352,
     2275, 2189, 2096, 1994, 1886, 1782, 1819, 1859, 1905, 1955, 2012, 2077, 2150, 2235, 2332, 2445,
     2578, 2735, 2923, 3152, 3433, 3784, 4227, 4798, 5544, 6527, 7821, 9470, 11379, 13099, 13798, 12950,
     11073, 9124, 7651, 6773, 6465, 6705, 7492, 8677, 9592, 9193, 7520, 5675, 4266, 3313, 2684, 2262,
     1976, 1778, 1641, 1550, 1496, 1476, 1492, 1554, 1683, 1922, 2336, 2942, 3363, 2985, 2259, 1733,
     1424, 1243, 1132, 1060, 1011, 977, 951, 932, 917, 905, 896, 888, 882, 877, 872, 869,
     866, 863, 860, 858, 857, 855, 854, 852, 851, 850, 849, 849, 848, 847, 847, 846,
     846,
    },
    {  // eh
     2864, 2864, 2863, 2862, 2861, 2860, 2859, 2857, 2856, 2853, 2851, 2848, 2844, 2840, 2835, 2829,
     2821, 2813, 2803, 2791, 2777, 2760, 2741, 2718, 2692, 2661, 2626, 2584, 2537, 2483, 2421, 2352,
     2275, 2189, 2107, 2169, 2239, 2318, 2409, 2512, 2632, 2771, 2934, 3127, 3357, 3635, 3973, 4390,
     4910, 5562, 6386, 7423, 8701, 10191, 11726, 12910, 13212, 12365, 10677, 8746, 7008, 5618, 4565, 3785,
     3209, 2783, 2467, 2231, 2056, 1928, 1838, 1780, 1750, 1749, 1778, 1843, 1959, 2146, 2449, 2951,
     3809, 5269, 7230, 7855, 6073, 4237, 3224, 2798, 2765, 3073, 3689, 4199, 3823, 2900, 2165, 1715,
     1447, 1281, 1174, 1100, 1048, 1011, 982, 960, 943, 930, 919, 910, 902, 896, 891, 886,
     883, 879, 877, 874, 872, 870, 869, 867, 866, 865, 864, 863, 863, 862, 861, 861,
     860,
    },
    {  // ee
     2864, 2864, 2863, 2862, 2861, 2860, 2859, 2857, 2856, 2853, 2851, 2848, 2844, 2840, 2844, 2910,
     2982, 3061, 3147, 3242, 3348, 3464, 3594, 3738, 3899, 4080, 4284, 4515, 4776, 5073, 5411, 5797,
     6237, 6739, 7306, 7941, 8637, 9378, 10123, 10811, 11350, 11637, 11585, 11158, 10398, 9410, 8321, 7243,
     6248, 5374, 4628, 4004, 3487, 3061, 2711, 2424, 2186, 1990, 1827, 1692, 1579, 1484, 1404, 1337,
     1281, 1234, 1194, 1161, 1134, 1112, 1094, 1081, 1072, 1066, 1065, 1068, 1076, 1090, 1110, 1139,
     1181, 1239, 1324, 1451, 1651, 1990, 2608, 3762, 5272, 5034, 3549, 2739, 2573, 2882, 3634, 4390,
     3996, 2870, 2037, 1563, 1294, 1134, 1032, 965, 919, 885, 860, 842, 827, 816, 807, 800,
     794, 789, 785, 781, 778, 776, 774, 772, 770, 769, 768, 767, 766, 765, 764, 764,
     763,
    },
};

// log2 of the phase increment of 50 Hz at 44.1 kHz, Q16: the curves' first point.
const i32 kLog2Increment50 = 1455906;

// The vowel's envelope at a frequency given as a phase increment (2^32 per
// cycle at 44.1 kHz), for Vowel 0..32767 (oo, oh, ah, eh, ee). Q12.
inline i32 vowel_curve_log2(i32 vowel, i32 log2_increment) {
    i32 at = (log2_increment - kLog2Increment50) * 16;  // Q16 points
    if (at < 0) at = 0;
    if (at >= static_cast<i32>(kVowelPoints - 1) << 16) at = (static_cast<i32>(kVowelPoints - 1) << 16) - 1;
    const u32 point = static_cast<u32>(at >> 16);
    const i32 between = (at & 0xffff) >> 1;  // Q15
    const i32 v = (vowel < 0 ? 0 : vowel > 32767 ? 32767 : vowel) * static_cast<i32>(kVowels - 1);
    u32 which = static_cast<u32>(v >> 15);
    i32 toward = v & 32767;
    if (which >= kVowels - 1) { which = kVowels - 2; toward = 32768; }
    const opxy::u16* a = kVowelCurves[which];
    const opxy::u16* b = kVowelCurves[which + 1];
    const i32 low = mix(a[point], b[point], toward), high = mix(a[point + 1], b[point + 1], toward);
    return mix(low, high, between);
}
inline i32 vowel_curve(i32 vowel, u32 increment) { return vowel_curve_log2(vowel, log2_q16(increment)); }

// The phase increment above which a mode is silent (6.5 kHz), and where it
// starts fading toward that (5 kHz): the resonators stay well inside their
// stable range, and the top of the spectrum stays soft.
const u32 kTopIncrement = 633045066u, kFadeIncrement = 486957743u;

// A mode's level (Q15) for a note: its body's level, coloured by the vowel as
// strongly as the family takes it, trimmed per family, and faded near the top.
// increment is the mode's own phase increment. This is what audio uses before
// Bright, Breath and the output gain; a screen can draw a body's spectrum
// from it with any reference note.
inline i32 mode_level(const Shape& shape, const Traits& traits, i32 vowel, u32 increment, i32 log2_increment) {
    if (increment >= kTopIncrement) return 0;
    const i32 curve = vowel_curve_log2(vowel, log2_increment);  // Q12
    const i32 colour = 4096 + ((curve - 4096) * (traits.vowel >> 3) >> 12);  // Q12, weighted by the family
    i32 level = (shape.level * colour >> 12) * traits.trim >> 12;
    if (level > 32767) level = 32767;
    if (increment > kFadeIncrement)  // 30106 is 2^30 over the fade's width in 4096ths
        level = level * (static_cast<i32>((kTopIncrement - increment) >> 12) * 30106 >> 15) >> 15;
    return level;
}
inline i32 mode_level(i32 body, i32 vowel, u32 k, u32 increment) {
    return mode_level(body_mode(body, k), body_traits(body), vowel, increment, log2_q16(increment));
}

// A mode's phase increment for a note (increment) at Body, before Spread.
inline u32 mode_increment(i32 body, u32 k, u32 increment) { return times(increment, body_mode(body, k).ratio); }

// ---------------------------------------------------------------------------
// Ring: the fundamental's ring time (to -60 dB) runs from 0.1 s to 20 s,
// exponentially, before the body scales it (vox 0.7x, pipe 0.85x, glass 1.5x).
// log2 of the seconds, Q16.
inline i32 ring_log2_seconds(i32 ring, i32 body) {
    const i32 r = ring < 0 ? 0 : ring > 32767 ? 32767 : ring;
    return -217706 + static_cast<i32>((static_cast<u32>(r) * 62619u) >> 12) + body_traits(body).ring_log2;  // log2 200 = 62619 / 8192
}

// Spread: the most any mode is detuned, in cents (0..15).
const i32 kSpreadCents = 15;

// ---------------------------------------------------------------------------
// Captions, at most five characters, for the page header.
inline void write_text(char* out, u32 capacity, const char* text) {
    u32 i = 0;
    for (; text[i] && i + 1 < capacity; ++i) out[i] = text[i];
    out[i] = 0;
}
// Body: the family's name at (or near) a family, else both families' initials.
inline void body_caption(char* out, u32 capacity, i32 body) {
    static const char* const names[kBodies] = {"vox", "str", "pipe", "glass"};
    static const char* const pairs[kBodies - 1] = {"vo/st", "st/pi", "pi/gl"};
    const Place p = body_place(body);
    if (p.fraction < 2048) write_text(out, capacity, names[p.family]);
    else if (p.fraction > 30720) write_text(out, capacity, names[p.family + 1]);
    else write_text(out, capacity, pairs[p.family]);
}
// Vowel: the nearest vowel.
inline void vowel_caption(char* out, u32 capacity, i32 vowel) {
    static const char* const names[kVowels] = {"oo", "oh", "ah", "eh", "ee"};
    const i32 v = vowel < 0 ? 0 : vowel > 32767 ? 32767 : vowel;
    write_text(out, capacity, names[(v * static_cast<i32>(kVowels - 1) + 16384) >> 15]);
}
// Ring: seconds, "0.4s" below ten seconds and "14s" above.
inline void ring_caption(char* out, u32 capacity, i32 ring, i32 body) {
    // Tenths of a second, rounded: 2^(log2 s + log2 10) + 0.5.
    const u32 tenths = (exp2_q16(ring_log2_seconds(ring, body) + 217706) + 32768) >> 16;
    char text[6];
    u32 n = 0;
    if (tenths >= 100) {
        const u32 s = (tenths + 5) / 10;
        if (s >= 10) text[n++] = static_cast<char>('0' + s / 10 % 10);
        text[n++] = static_cast<char>('0' + s % 10);
    } else {
        text[n++] = static_cast<char>('0' + tenths / 10);
        text[n++] = '.';
        text[n++] = static_cast<char>('0' + tenths % 10);
    }
    text[n++] = 's';
    text[n] = 0;
    write_text(out, capacity, text);
}
// Spread: cents, "0c" to "15c".
inline void spread_caption(char* out, u32 capacity, i32 spread) {
    const i32 s = spread < 0 ? 0 : spread > 32767 ? 32767 : spread;
    const u32 cents = static_cast<u32>((s * kSpreadCents + 16384) >> 15);
    char text[4];
    u32 n = 0;
    if (cents >= 10) text[n++] = static_cast<char>('0' + cents / 10);
    text[n++] = static_cast<char>('0' + cents % 10);
    text[n++] = 'c';
    text[n] = 0;
    write_text(out, capacity, text);
}
}
