#pragma once
#include <cstddef>
#include <string>
#include <vector>

// ── Corpus-derived drum phrase bank ──
// Multi-bar accent phrases extracted from the drum MIDI corpus
// (E:\midi\[1] Drum MIDIs) and converted to RhythmPatternGenerator DSL
// strings (each phrase = bars x grid steps of 'x'/'-').
// DATA ONLY — no engine behavior. Provenance:
//   tools/analyze_drum_midis.mjs    (SMF parser + role/pitch stats)
//   tools/extract_phrase_bank.mjs   (multi-bar phrase extraction + dedupe)
//   tools/emit_pattern_bank.mjs     (phrase -> DSL conversion)
// The bank is header-only so adding phrases needs no source/CMake change.
namespace HDAW
{

struct RhythmicPhrase
{
    const char* id;      // unique id, e.g. "hats_hh1_4bar"
    const char* role;    // kick / snare / clap / hats / openHat / perc / tom / crash / ride
    int bars;            // loop length in bars
    int grid;            // steps per bar (16 = sixteenths)
    const char* dsl;     // bars*grid chars: 'x' hit, '-' rest
    int pitch;           // GM default pitch for the role
    int bpm;             // source loop tempo
    const char* source;  // originating file
};

inline const RhythmicPhrase* rhythmPhrases()
{
    static const RhythmicPhrase k[] = {
        { "hats_hh1_4bar",  "hats",  4, 16, "x-x-x-x-x-x-x-x---x-x-xxx-x---x-xxx-x-x-x-x-x-x-xxx-x-x-x-x-xxx-", 42, 152, "HH Midi 1.mid" },
        { "hats_hh10_4bar", "hats",  4, 16, "x---x-x-x-x-x-----x-x-x-x-x-x---x-x-x-x-x---x-x---x-x-x-x-x-x-xx", 42, 160, "HH Midi 10.mid" },
        { "hats_hh11_4bar", "hats",  4, 16, "x-x-x---x-x-x-x-xxx-x-x-x-x-xxxxx-x-x-x-x---x-x-x-x-xxxxx-x-x-x-", 42, 172, "HH Midi 11.mid" },
        { "hats_hh12_2bar", "hats",  2, 16, "x-x---x---x-x-----x-x-x---x-x-x-", 42, 165, "HH Midi 12.mid" },
        { "hats_hh2_4bar",  "hats",  4, 16, "x---x-x-x-x-x-x---x-x-x-x-x---x-x---x-x-x---x-x-xxx-x-x-x-x-x-x-", 42, 160, "HH Midi 2.mid" },
        { "hats_hh3_4bar",  "hats",  4, 16, "x-x---x-x---x-x-----x-x-x---x-xxx-x-x---x---x-x---x-xxx-x-x-xxx-", 42, 157, "HH Midi 3.mid" },
        { "hats_hh4_4bar",  "hats",  4, 16, "x-x-x-x-x-x-x-x---x-x---x-x-x-xxx---x-x-x---x-x---x-x-xxx-x---x-", 42, 157, "HH Midi 4.mid" },
        { "hats_hh7_4bar",  "hats",  4, 16, "x---x-x-x---x-x---x-x-x-x-x---x-x---x-x-x---x-x-x-x-x-x-x-x-xxx-", 42, 159, "HH Midi 7.mid" },
        { "hats_hh8_4bar",  "hats",  4, 16, "x-x-x---xxx-x-x---x-xxx-x-x-x-x-x-x-x-x-x---x-x-x-x-x-xxx-x-x---", 42, 160, "HH Midi 8.mid" },
        { "hats_hh9_4bar",  "hats",  4, 16, "x---x-x-x-x-x-x---x-x-xxx-x---x-x---x-x-x---x-x-xxx-x-x-x-x-x-x-", 42, 159, "HH Midi 9.mid" },

        { "perc_p1_4bar",   "perc",  4, 16, "--x-------x-----------------------x-------x---------------x-x-x-", 60, 140, "[BP] Perc Midi 1.mid" },
        { "perc_p10_1bar",  "perc",  1, 16, "----x-x---x-----", 60, 172, "[BP] Perc Midi 10.mid" },
        { "perc_p2_8bar",   "perc",  8, 16, "----------x-------------x-x---x-------------------------x-x---x-------------------------x-x---x-------------------------x-x---x-", 60, 140, "[BP] Perc Midi 2.mid" },
        { "perc_p3_2bar",   "perc",  2, 16, "----x-----x-x-x-----------x-----", 60, 140, "[BP] Perc Midi 3.mid" },
        { "perc_p4_8bar",   "perc",  8, 16, "----------------------x--------------------------xx---x-------------------------------x-------------------------------x---------", 60, 140, "[BP] Perc Midi 4.mid" },
        { "perc_p6_4bar",   "perc",  4, 16, "------x---x-x---------x---x-x---------x---x-x-------x-x---------", 60, 140, "[BP] Perc Midi 6.mid" },
        { "perc_p7_4bar",   "perc",  4, 16, "--x-------x---------------xxx-x---x-------x-------x---x---xxx-x-", 60, 140, "[BP] Perc Midi 7.mid" },

        { "snare_s1_8bar",  "snare", 8, 16, "--------------x-------------------------------x---x---------x-x---------------x-------------------------------x---x-----xxx-x-x-", 38, 165, "[BP] Snare Midi 1.mid" },
        { "snare_s2_4bar",  "snare", 4, 16, "--x---------------x---------------------------x---x-x-------x-x-", 38, 165, "[BP] Snare Midi 2.mid" },
        { "snare_s3_8bar",  "snare", 8, 16, "----------------------------------------------------------x---x-----------------------------------------x-----x-----x---x-x---x-", 38, 165, "[BP] Snare Midi 3.mid" },
        { "snare_s4_8bar",  "snare", 8, 16, "----------------------------------------------------x-x---x-x-x-----------------------------------------------x---x-x-x-xxx-x-x-", 38, 165, "[BP] Snare Midi 4.mid" },
        { "snare_s5_8bar",  "snare", 8, 16, "--------x---------x-----x-----x---------x---------x-----x---------------x---------x-----x-----x---------x-----x---x-x---x---x-x-", 38, 165, "[BP] Snare Midi 5.mid" },

        { "clap_c1_1bar",   "clap",  1, 16, "--------x-------", 39, 166, "Standard Clap Midi.mid" },

        // ── Corpus phrases from Mix Elite / Clark Audio / CLAP packs ──
        // (single-instrument loop packs; role from filename, curated to
        // multi-bar + cross-file-verified grooves). Generated via
        // tools/curate_bank.mjs from drum_phrase_bank_packs.json.
        { "kick_4bar_1",   "kick",  4, 16, "x-----------x-x-----x-----------x-----------x-x-----x-----x-----", 36, 140, "Mix Elite - MDV1 - Kick 10" },
        { "kick_4bar_2",   "kick",  4, 16, "x-----x-----x-----x-x---x-------x-----------x-------x-----x-----", 36, 181, "Mix Elite - MDV1 - Kick 11" },
        { "kick_4bar_3",   "kick",  4, 16, "x-x-----------x-----x---------x-x-------------x-----x-----------", 36, 172, "Mix Elite - MDV1 - Kick 12" },
        { "kick_4bar_4",   "kick",  4, 16, "x-x-----------------x-----x-----x-----x-------------x-----x-----", 36, 166, "Mix Elite - MDV1 - Kick 13" },
        { "kick_4bar_5",   "kick",  4, 16, "x-----------------x-x-----x-----x-x-----------------x-----x-----", 36, 160, "Mix Elite - MDV1 - Kick 14" },
        { "kick_4bar_6",   "kick",  4, 16, "x-x-----------------x-----x-----x-------------------x-----x-----", 36, 155, "Mix Elite - MDV1 - Kick 15" },
        { "kick_4bar_7",   "kick",  4, 16, "x-x-----------------x-----x-----x-----------------x-x-----x-----", 36, 130, "Mix Elite - MDV1 - Kick 16" },
        { "kick_4bar_8",   "kick",  4, 16, "x-------------------x-----------x-----------------x-x-----x-----", 36, 140, "Mix Elite - MDV1 - Kick 17" },

        { "snare_2bar_1",  "snare", 2, 16, "--------x-----x---x-----x-------", 38, 115, "Mix Elite - MDV1 - Snare 10" },
        { "snare_4bar_2",  "snare", 4, 16, "--x-----x---------------x-----x---------x---------------x---x---", 38, 172, "Mix Elite - MDV1 - Snare 1" },
        { "snare_4bar_3",  "snare", 4, 16, "--x-----x---------x-----x---------------x-----x---x-----x-------", 38, 107, "Mix Elite - MDV1 - Snare 11" },
        { "snare_4bar_4",  "snare", 4, 16, "--------x-------------x-x---------------x---------x-----x-------", 38, 140, "Mix Elite - MDV1 - Snare 14" },
        { "snare_4bar_5",  "snare", 4, 16, "--------x---------------x---xxxx--------x---------x-----x-------", 38, 120, "Mix Elite - MDV1 - Snare 15" },
        { "snare_4bar_6",  "snare", 4, 16, "--------x-----x---x-----x---------------x-----x---x---x-x-------", 38, 135, "Mix Elite - MDV1 - Snare 16" },
        { "snare_4bar_7",  "snare", 4, 16, "------x-x---------------x-xx--x---------x---------xx--x-x-------", 38, 108, "Mix Elite - MDV1 - Snare 17" },
        { "snare_4bar_8",  "snare", 4, 16, "------x-x-----x-----x-x-x---------x-----x---------------x-------", 38, 109, "Mix Elite - MDV1 - Snare 18" },

        { "clap_4bar_1",   "clap",  4, 16, "x--x--xx--x-x---x--x--xx--x-x---x--x--xx--x-x---x--x--xx--x-xx--", 39, 140, "CLAP MIDI 2" },
        { "clap_4bar_2",   "clap",  4, 16, "---x-----xx-x------x-----xx-x------x-----xx-x------x-----xx-x-x-", 39, 140, "CLAP MIDI 4" },
        { "clap_4bar_3",   "clap",  4, 16, "xxx---x---xxxx--xx-xx-x---x-x---x-x---x---xxxx--xx-xx-x---x-x---", 39, 140, "CLAP MIDI 7" },
        { "clap_4bar_4",   "clap",  4, 16, "x---x---x--x--x-x---x---x--x--xxx--x--x-x--x--x-x---x---x--x--x-", 39, 140, "CLAP MIDI 8" },
        { "clap_4bar_5",   "clap",  4, 16, "----xxxx---x--x-----xxxx---x--x-----xxxx--x-x-------xxxx--x-x---", 39, 140, "CLAP MIDI 9" },

        { "hats_1bar_1",   "hats",  1, 16, "xxxxxxxxxxxxxxxx", 42, 71, "Mix Elite - MDV1 - Hat 10" },
        { "hats_4bar_2",   "hats",  4, 16, "xxx-x-x-x-x-xxx-x-x-x-x-xxxxxxx-xxx-x-x-xxx-x-x-x-x-x-x-xxxxx-x-", 42, 130, "Clark Audio - MIDI HiHat - 100" },
        { "hats_2bar_3",   "hats",  2, 16, "x-x-x-xxx-x-xxxxx-x-x-x-x-x-x-x-", 42, 94, "Mix Elite - MDV1 - Hat 125" },
        { "hats_2bar_4",   "hats",  2, 16, "x-xxx-x-x-x-x-x-x-x-x-x-x-x-x-x-", 42, 100, "Mix Elite - MDV1 - Hat 140" },
        { "hats_2bar_5",   "hats",  2, 16, "x-x-x-xxx-x-x-x-x-x-x-x-x-x-x-x-", 42, 105, "Mix Elite - MDV1 - Hat 105" },
        { "hats_2bar_6",   "hats",  2, 16, "x-x-xxxxx-x-x-x-x-x-x-x-x-x-x-x-", 42, 119, "Mix Elite - MDV1 - Hat 111" },
        { "hats_2bar_7",   "hats",  2, 16, "x-xxx-x-x-x-x-xxx-x-x-x-x-x-x-x-", 42, 99, "Mix Elite - MDV1 - Hat 115" },
        { "hats_2bar_8",   "hats",  2, 16, "x-x-x-xxx-x-x-x-x-x-xxx-x-x-x-x-", 42, 79, "Mix Elite - MDV1 - Hat 122" },
        { "hats_2bar_9",   "hats",  2, 16, "x-x-x-x-x-x-xxx-x-x-x-x-x-x-x-x-", 42, 136, "Mix Elite - MDV1 - Hat 13" },
        { "hats_2bar_10",  "hats",  2, 16, "x-x-xxx-x-x-xxx-x-x-x-xxx-x-x-x-", 42, 157, "Mix Elite - MDV1 - Hat 145" },

        // ── Full-kit grooves (role-from-pitch; Toontrack Rock + Psytrance) ──
        // Curated from kit_phrase_bank.json — the multi-bar drum-fill grooves.
        // (Most full-kit pack content is trivial backbeats / 4-on-floor /
        // sparse accents that the euclidean generator already covers, so only
        // the genuine multi-bar fill grooves were kept.)
        { "kick_2bar_1",   "kick",  2, 16, "x---x---x---x--x----x---x---x---", 36, 117, "Toontrack Variation_07" },
        { "kick_8bar_3",   "kick",  8, 16, "-----x-----x-x-x-----x-----x-x-x-----x-----x-x-x-----x-----x---xx----x-----x-x-x-----x-----x-xxx-----x-----x-x-x-----x-x-----x-x", 36, 75, "Toontrack Variation_01" },
        { "kick_8bar_4",   "kick",  8, 16, "x--x----xx-x----x--x----xx-x----x--x----xx-x----x--x----xx-x-x--x--x----xx-x----x--x----xx-x----x--x----xx-x-x--x--x----xx-x----", 36, 75, "Toontrack Variation_03" },
        { "kick_8bar_5",   "kick",  8, 16, "---x---x---x-xxx---x---x---x-xxx---x---x---x-xxx---x---x---x-x-x---x---x---x-x-x---x---x---x-x-x---x---x---x-x-x---x---x-----x--", 36, 75, "Toontrack Variation_04" },
        { "kick_8bar_6",   "kick",  8, 16, "x-x-------xx---x--x-------xx---x--x-------xx---x--x-------x----x--x-------xx---x--x-------xx-x-x--x-------xx---x--x-----x-x-----", 36, 75, "Toontrack Variation_05" },
        { "snare_8bar_5",  "snare", 8, 16, "----x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x------xxxxx", 38, 120, "Toontrack Variation_02" },
        { "snare_8bar_6",  "snare", 8, 16, "----x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x-------x-----xxxxxx", 38, 120, "Toontrack Variation_05" },
        { "ride_4bar_2",   "ride",  4, 16, "xxx-xxxxxxx----xxxx-xxxxxx----xxxxx--xxxxxx-------x---xx---x--x-", 51, 120, "Bass-Psytrance High 1" },
    };
    return k;
}

inline int rhythmPhraseCount()
{
    return 62;
}

inline const RhythmicPhrase* findRhythmicPhrase(const char* id)
{
    const RhythmicPhrase* all = rhythmPhrases();
    for (int i = 0; i < rhythmPhraseCount(); ++i)
        if (std::string(all[i].id) == id)
            return &all[i];
    return nullptr;
}

// index-th phrase with the given role (0-based); null if none.
inline const RhythmicPhrase* findRhythmicPhraseByRole(const char* role, int index)
{
    const RhythmicPhrase* all = rhythmPhrases();
    int seen = 0;
    for (int i = 0; i < rhythmPhraseCount(); ++i)
        if (std::string(all[i].role) == role)
        {
            if (seen == index)
                return &all[i];
            ++seen;
        }
    return nullptr;
}

inline std::vector<const RhythmicPhrase*> rhythmPhrasesForRole(const char* role)
{
    std::vector<const RhythmicPhrase*> out;
    const RhythmicPhrase* all = rhythmPhrases();
    for (int i = 0; i < rhythmPhraseCount(); ++i)
        if (std::string(all[i].role) == role)
            out.push_back(&all[i]);
    return out;
}

} // namespace HDAW
