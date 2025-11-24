#pragma once
#include <vector>
#include <random>
#include "MarkovModel.h"
#include "RhythmModel.h"
#include "MidiParser.h"

class MelodyGenerator {
public:
    MelodyGenerator(MarkovModel& melodyModel, RhythmModel& rhythmModel, int melodyOrder = 2, int historyMax = 8);
    std::vector<NoteEvent> generate(int length, int startPitch = 60, int minPitch = 0, int maxPitch = 127, double melodyTemp = 1.0, double rhythmTemp = 1.0, int startVelocity = 80, bool enforceScale = false, const std::vector<int>& allowedPitchClasses = {}) ;
private:
    MarkovModel& melodyModel_;
    RhythmModel& rhythmModel_;
    int melodyOrder_;
    int historyMax_;
    mutable std::mt19937 rng_;
    int clampPitch(int p, int minP, int maxP) const;
    bool pitchClassAllowed(int pitch, const std::vector<int>& allowed) const;
    int nearestAllowedPitch(int pitch, int minP, int maxP, const std::vector<int>& allowed) const;
};
