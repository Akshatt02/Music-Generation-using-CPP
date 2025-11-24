#include "MelodyGenerator.h"
#include <chrono>
#include <algorithm>
#include <iostream>

MelodyGenerator::MelodyGenerator(MarkovModel& melodyModel, RhythmModel& rhythmModel, int melodyOrder, int historyMax) : melodyModel_(melodyModel), rhythmModel_(rhythmModel), melodyOrder_(std::max(1, melodyOrder)), historyMax_(std::max(melodyOrder_, historyMax)) {
    std::random_device rd;
    rng_.seed(rd() ^ static_cast<unsigned long>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
}

int MelodyGenerator::clampPitch(int p, int minP, int maxP) const {
    if (p < minP) return minP;
    if (p > maxP) return maxP;
    return p;
}

bool MelodyGenerator::pitchClassAllowed(int pitch, const std::vector<int>& allowed) const {
    if (allowed.empty()) return true;
    int pc = ((pitch % 12) + 12) % 12;
    for (int a : allowed) if ((a % 12 + 12) % 12 == pc) return true;
    return false;
}

int MelodyGenerator::nearestAllowedPitch(int pitch, int minP, int maxP, const std::vector<int>& allowed) const {
    if (allowed.empty()) return clampPitch(pitch, minP, maxP);
    int best = pitch;
    for (int d = 0; d <= 12; ++d) {
        int up = pitch + d;
        if (up <= maxP && pitchClassAllowed(up, allowed)) return up;
        int down = pitch - d;
        if (down >= minP && pitchClassAllowed(down, allowed)) return down;
    }
    return clampPitch(pitch, minP, maxP);
}

std::vector<NoteEvent> MelodyGenerator::generate(int length, int startPitch, int minPitch, int maxPitch, double melodyTemp, double rhythmTemp, int startVelocity, bool enforceScale, const std::vector<int>& allowedPitchClasses) {
    std::vector<NoteEvent> out;
    if (length <= 0) return out;

    std::vector<int> pitchHistory;
    std::vector<double> durHistory;

    pitchHistory.push_back(startPitch);

    double timeCursor = 0.0;

    for (int i = 0; i < length; ++i) {
        std::vector<int> histForMelody;
        int histTake = std::min((int)pitchHistory.size(), melodyOrder_);
        if (histTake > 0) histForMelody.insert(histForMelody.end(), pitchHistory.end() - histTake, pitchHistory.end());

        int sampledPitch = melodyModel_.sampleNext(histForMelody, melodyTemp);

        if (enforceScale) {
            if (!pitchClassAllowed(sampledPitch, allowedPitchClasses)) {
                sampledPitch = nearestAllowedPitch(sampledPitch, minPitch, maxPitch, allowedPitchClasses);
            }
        }

        sampledPitch = clampPitch(sampledPitch, minPitch, maxPitch);

        std::vector<double> histForRhythm;
        int rhTake = std::min((int)durHistory.size(), historyMax_);
        if (rhTake > 0) histForRhythm.insert(histForRhythm.end(), durHistory.end() - rhTake, durHistory.end());

        double sampledDur = rhythmModel_.sampleNext(histForRhythm, rhythmTemp);

        if (!(sampledDur > 0.0)) sampledDur = 0.25;

        NoteEvent ne;
        ne.pitch = sampledPitch;
        ne.startTime = timeCursor;
        ne.duration = sampledDur;
        out.push_back(ne);

        timeCursor += sampledDur;
        pitchHistory.push_back(sampledPitch);
        if ((int)pitchHistory.size() > historyMax_) pitchHistory.erase(pitchHistory.begin(), pitchHistory.begin() + (pitchHistory.size() - historyMax_));
        durHistory.push_back(sampledDur);
        if ((int)durHistory.size() > historyMax_) durHistory.erase(durHistory.begin(), durHistory.begin() + (durHistory.size() - historyMax_));
    }

    return out;
}
