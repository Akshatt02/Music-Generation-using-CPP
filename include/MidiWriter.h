#pragma once
#include <string>
#include <vector>
#include "MidiParser.h"
#include <stdint.h>

class MidiWriter {
public:
    bool write(const std::string& outPath, const std::vector<NoteEvent>& notes, int ppq = 480, uint32_t microsecondsPerQuarter = 500000, int channel = 0, int velocity = 90) const;
};
