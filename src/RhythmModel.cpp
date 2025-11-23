#include "RhythmModel.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>

static long long llgcd(long long a, long long b) {
    if (a == 0) return b;
    if (b == 0) return a;
    a = std::llabs(a); b = std::llabs(b);
    while (b) {
        long long t = a % b;
        a = b; b = t;
    }
    return a;
}

RhythmModel::RhythmModel(int order, double unitScale)
    : order_(std::max(1, order)), unit_(0.0), unitScale_(unitScale), markov_(order)
{}

void RhythmModel::computeUnitFromDurations(const std::vector<double>& durations) {
    std::vector<long long> ints;
    ints.reserve(durations.size());
    for (double d : durations) {
        if (!(d > 0.0)) continue;
        long long v = static_cast<long long>(std::llround(d * unitScale_));
        if (v <= 0) continue;
        ints.push_back(v);
    }
    if (ints.empty()) {
        unit_ = 0.0;
        return;
    }

    long long g = ints[0];
    for (size_t i = 1; i < ints.size(); ++i) g = llgcd(g, ints[i]);
    if (g <= 0) {
        unit_ = 0.0;
        return;
    }

    unit_ = static_cast<double>(g) / unitScale_;
    const double minUnit = 1e-4;
    if (unit_ < minUnit) unit_ = minUnit;
}

void RhythmModel::train(const std::vector<double>& durations) {
    if (durations.empty()) return;
    if (!hasUnit()) {
        computeUnitFromDurations(durations);
        if (!hasUnit()) {
            std::cerr << "RhythmModel::train: failed to compute quantization unit\n";
            return;
        }
    }

    std::vector<int> tokens;
    tokens.reserve(durations.size());
    for (double d : durations) {
        if (d <= 0.0) continue;
        int tok = durationToToken(d);
        tokens.push_back(tok);
    }

    markov_.train(tokens);
}

void RhythmModel::trainMany(const std::vector<std::vector<double>>& sequences) {
    bool unitComputed = hasUnit();
    for (const auto &s : sequences) {
        if (!unitComputed) {
            computeUnitFromDurations(s);
            unitComputed = hasUnit();
        }
        train(s);
    }
}

int RhythmModel::durationToToken(double d) const {
    if (!hasUnit()) return 0;
    int tok = static_cast<int>(std::llround(d / unit_));
    if (tok < 0) tok = 0;
    return tok;
}

double RhythmModel::tokenToDuration(int token) const {
    if (!hasUnit()) return 0.0;
    return static_cast<double>(token) * unit_;
}

double RhythmModel::sampleNext(const std::vector<double>& history, double temperature) const {
    if (!hasUnit()) {
        std::cerr << "RhythmModel::sampleNext: unit not initialized. Returning 0.0\n";
        return 0.0;
    }
    std::vector<int> histTokens;
    histTokens.reserve(history.size());
    for (double d : history) {
        if (d <= 0.0) continue;
        histTokens.push_back(durationToToken(d));
    }
    int tok = markov_.sampleNext(histTokens, temperature);
    return tokenToDuration(tok);
}
