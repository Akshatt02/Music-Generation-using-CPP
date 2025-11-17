#pragma once
#include <vector>
#include <string>

struct NoteEvent {
    int pitch;
    double startTime;
    double duration;
};

class Parser {
public:
    std::vector<NoteEvent> parseMidiFile(const std::string& path);
    std::vector<int> parseMelodyTxt(const std::string& path);
    std::vector<double> parseDurationTxt(const std::string& path);
    void exportMelodyTxt(const std::vector<NoteEvent>& notes, const std::string& outPath);
    void exportDurationTxt(const std::vector<NoteEvent>& notes, const std::string& outPath);
};
