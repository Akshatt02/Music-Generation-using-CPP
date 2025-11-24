#include "MidiParser.h"
#include "MarkovModel.h"
#include "RhythmModel.h"
#include "MelodyGenerator.h"
#include "MidiWriter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <set>
#include <map>
#include <numeric>

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using clock = std::chrono::high_resolution_clock;

    const std::string midiFolder = "../data/raw_midis/";
    const std::string melodyFolder = "../data/melodies/";
    const std::string durationFolder = "../data/durations/";
    const std::string outputFolder = "../output/";
    const std::string generatedSeqPath = outputFolder + "generated_seq.txt";
    const std::string generatedMidPath = outputFolder + "generated.mid";

    const int markovOrder = 2;
    const int historyMax = 8;
    const int generateLength = 128;
    const int startPitch = 60;
    const int minPitch = 48;
    const int maxPitch = 84;
    const double melodyTemp = 1.0;
    const double rhythmTemp = 1.0;
    const int midiPPQ = 480;
    const uint32_t tempoMicro = 500000;
    const int midiChannel = 0;
    const int midiVelocity = 90;

    fs::create_directories(melodyFolder);
    fs::create_directories(durationFolder);
    fs::create_directories(outputFolder);

    Parser parser;

    std::cout << "Phase A: Parsing MIDI files and exporting text training files...\n";
    auto t0 = clock::now();

    size_t midiFiles = 0;
    size_t totalParsedNotes = 0;
    std::map<std::string, size_t> perFileNotes;

    if (fs::exists(midiFolder)) {
        for (auto& entry : fs::directory_iterator(midiFolder)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".mid" || entry.path().extension() == ".midi") {
                std::string stem = entry.path().stem().string();
                std::string melodyOut   = (fs::path(melodyFolder) / (stem + ".txt")).string();
                std::string durationOut = (fs::path(durationFolder) / (stem + "_dur.txt")).string();

                auto events = parser.parseMidiFile(entry.path().string());
                parser.exportMelodyTxt(events, melodyOut);
                parser.exportDurationTxt(events, durationOut);

                midiFiles++;
                totalParsedNotes += events.size();
                perFileNotes[stem] = events.size();
                std::cout << "  Processed: " << stem << " (" << events.size() << " notes)\n";
            }
        }
    } else {
        std::cout << "Warning: midiFolder '" << midiFolder << "' does not exist. Skipping conversion step.\n";
    }

    auto t1 = clock::now();
    auto durParseMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "Parsing/export stage done. MIDI files processed: " << midiFiles << ", total notes: " << totalParsedNotes << ", time: " << durParseMs << " ms\n\n";

    std::cout << "Phase B: Loading training sequences from text files...\n";
    std::vector<std::vector<int>> melodySeqs;
    std::vector<std::vector<double>> durSeqs;

    if (fs::exists(melodyFolder)) {
        for (auto &entry : fs::directory_iterator(melodyFolder)) {
            if (!entry.is_regular_file()) continue;
            auto p = entry.path();
            if (p.extension() == ".txt" && p.stem().string().find("_dur") == std::string::npos) {
                auto seq = parser.parseMelodyTxt(p.string());
                if (!seq.empty()) {
                    melodySeqs.push_back(seq);
                }
                std::string durPath = (fs::path(durationFolder) / (p.stem().string() + "_dur.txt")).string();
                if (fs::exists(durPath)) {
                    auto dseq = parser.parseDurationTxt(durPath);
                    if (!dseq.empty()) durSeqs.push_back(dseq);
                }
            }
        }
    }

    std::cout << "  Melody sequences found: " << melodySeqs.size() << "\n";
    std::cout << "  Duration sequences found: " << durSeqs.size() << "\n\n";

    std::cout << "Phase C: Training Markov melody model and rhythm model...\n";
    auto t2 = clock::now();

    MarkovModel melodyModel(markovOrder);
    melodyModel.trainMany(melodySeqs);

    RhythmModel rhythmModel(markovOrder);
    if (!durSeqs.empty()) {
        rhythmModel.trainMany(durSeqs);
    } else {
        std::cout << "  Warning: no duration sequences available; rhythm model will fallback to defaults.\n";
    }

    auto t3 = clock::now();
    auto durTrainMs = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    std::cout << "Training time: " << durTrainMs << " ms\n\n";

    std::cout << "Phase D: Model metrics\n";
    std::cout << "  Melody vocabulary size (distinct tokens): " << melodyModel.vocabularySize() << "\n";

    std::set<std::vector<int>> uniqueHistories;
    for (const auto &seq : melodySeqs) {
        if (seq.size() < 2) continue;
        for (size_t i = 1; i < seq.size(); ++i) {
            for (int k = 1; k <= markovOrder; ++k) {
                if (i < static_cast<size_t>(k)) break;
                std::vector<int> h(seq.begin() + (i - k), seq.begin() + i);
                uniqueHistories.insert(h);
            }
        }
    }

    size_t transitionsEntries = 0;
    size_t transitionsTypes = 0;
    for (const auto &h : uniqueHistories) {
        auto cnts = melodyModel.getCountsForHistory(h);
        if (!cnts.empty()) {
            transitionsEntries += cnts.size();
            transitionsTypes += std::accumulate(cnts.begin(), cnts.end(), size_t(0), [](size_t acc, const std::pair<int,uint32_t>& p){ 
                return acc + p.second; 
            });
        }
    }
    std::cout << "  Unique conditioning histories considered: " << uniqueHistories.size() << "\n";
    std::cout << "  Transition entries (history -> possible next tokens): " << transitionsEntries << "\n";
    std::cout << "  Transition observations (sum of counts): " << transitionsTypes << "\n";

    if (rhythmModel.hasUnit()) {
        std::cout << "  Rhythm quantization unit (seconds): " << rhythmModel.unit() << "\n";

        std::set<int> durTokens;
        for (const auto &s : durSeqs) {
            for (double d : s) {
                if (d <= 0.0) continue;
                durTokens.insert(rhythmModel.durationToToken(d));
            }
        }
        std::cout << "  Distinct duration tokens seen: " << durTokens.size() << "\n";
    } else {
        std::cout << "  Rhythm model has no unit (no durations trained)\n";
    }
    std::cout << "\n";

    std::cout << "Phase E: Generating melody (length = " << generateLength << ")...\n";
    auto t4 = clock::now();

    MelodyGenerator gen(melodyModel, rhythmModel, markovOrder, historyMax);
    auto generatedNotes = gen.generate(generateLength, startPitch, minPitch, maxPitch, melodyTemp, rhythmTemp);

    auto t5 = clock::now();
    auto durGenMs = std::chrono::duration_cast<std::chrono::milliseconds>(t5 - t4).count();
    std::cout << "  Generation time: " << durGenMs << " ms\n";
    std::cout << "  Generated notes: " << generatedNotes.size() << "\n\n";

    {
        std::ofstream out(generatedSeqPath);
        if (out.is_open()) {
            for (const auto &n : generatedNotes) {
                out << n.pitch << ' ' << n.startTime << ' ' << n.duration << '\n';
            }
            out.close();
            std::cout << "  Wrote generated sequence -> " << generatedSeqPath << "\n";
        } else {
            std::cerr << "  Failed to write generated sequence file: " << generatedSeqPath << "\n";
        }
    }

    std::cout << "Phase F: Writing MIDI file: " << generatedMidPath << " ...\n";
    auto t6 = clock::now();
    MidiWriter writer;
    bool ok = writer.write(generatedMidPath, generatedNotes, midiPPQ, tempoMicro, midiChannel, midiVelocity);
    auto t7 = clock::now();
    auto durWriteMs = std::chrono::duration_cast<std::chrono::milliseconds>(t7 - t6).count();

    if (!ok) {
        std::cerr << "  MidiWriter failed to write MIDI file.\n";
    } else {
        std::uintmax_t fileSize = 0;
        if (fs::exists(generatedMidPath)) {
            fileSize = fs::file_size(generatedMidPath);
        }
        std::cout << "  MIDI written successfully. file size: " << fileSize << " bytes. write time: " << durWriteMs << " ms\n";
        std::cout << "  Open the MIDI in MuseScore or any DAW to play.\n";
    }

    std::cout << "Parsed MIDI files: " << midiFiles << "\n";
    std::cout << "Total parsed notes: " << totalParsedNotes << "\n";
    if (midiFiles > 0) {
        std::cout << "Avg notes / MIDI: " << (totalParsedNotes / static_cast<double>(midiFiles)) << "\n";
    }
    std::cout << "Melody sequences used for training: " << melodySeqs.size() << "\n";
    std::cout << "Duration sequences used for training: " << durSeqs.size() << "\n";
    std::cout << "Melody vocab size: " << melodyModel.vocabularySize() << "\n";
    std::cout << "Transition entries: " << transitionsEntries << "\n";
    std::cout << "Transition observations: " << transitionsTypes << "\n";
    if (rhythmModel.hasUnit()) {
        std::cout << "Rhythm unit (s): " << rhythmModel.unit() << "\n";
    } else {
        std::cout << "Rhythm unit: (not set)\n";
    }
    std::cout << "Timings (ms): parse/export=" << durParseMs << ", train=" << durTrainMs << ", generate=" << durGenMs << ", write_mid=" << durWriteMs << "\n";
    std::cout << "Generated MIDI: " << (ok ? generatedMidPath : "(failed)") << "\n";

    return 0;
}
