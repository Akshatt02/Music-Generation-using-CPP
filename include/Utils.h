#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <stdint.h>

namespace Utils {

    uint16_t readBE16(std::ifstream& file);
    uint32_t readBE32(std::ifstream& file);
    uint32_t readVarLen(std::ifstream& file);

    int noteNameToMidi(const std::string& name);
    std::string midiToNoteName(int midi);

    int randomChoice(const std::vector<int>& values);
}
