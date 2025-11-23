#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "MarkovModel.h"

class RhythmModel {
public:
    explicit RhythmModel(int order = 2, double unitScale = 1000.0);
    void train(const std::vector<double>& durations);
    void trainMany(const std::vector<std::vector<double>>& sequences);
    double sampleNext(const std::vector<double>& history, double temperature = 1.0) const;
    double unit() const { return unit_; }
    bool hasUnit() const { return unit_ > 0.0; }
    int durationToToken(double d) const;
    double tokenToDuration(int token) const;
private:
    void computeUnitFromDurations(const std::vector<double>& durations);
    int order_;
    double unit_;
    double unitScale_;
    MarkovModel markov_;
};