#include "MidiParser.h"
#include <iostream>
#include <filesystem>
#include <chrono>

int main() {
    namespace fs = std::filesystem;

    Parser parser;

    std::string midiFolder = "../data/raw_midis/";
    std::string melodyFolder = "../data/melodies/";
    std::string durationFolder = "../data/durations/";

    fs::create_directories(melodyFolder);
    fs::create_directories(durationFolder);

    auto start = std::chrono::high_resolution_clock::now();
    for (auto& entry : fs::directory_iterator(midiFolder)) {
        if (entry.path().extension() == ".mid") {
            std::string filename = entry.path().stem().string();

            std::string melodyOut   = melodyFolder   + filename + ".txt";
            std::string durationOut = durationFolder + filename + "_dur.txt";

            auto events = parser.parseMidiFile(entry.path().string());

            parser.exportMelodyTxt(events, melodyOut);
            parser.exportDurationTxt(events, durationOut);

            std::cout << "Processed: " << filename << "\n";
        }
    }
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

    std::cout << "All MIDI files processed in " << duration.count() << " ms\n";
}
