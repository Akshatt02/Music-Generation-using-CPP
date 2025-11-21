#include "MarkovModel.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>

MarkovModel::MarkovModel(int order) : order_(std::max(1, order)) {
    std::random_device rd;
    rng_.seed(rd() ^ static_cast<unsigned long>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
}

void MarkovModel::train(const std::vector<int>& sequence) {
    if (sequence.empty()) return;

    for (int t : sequence) {
        unigramCounts_[t] += 1;
    }

    const size_t n = sequence.size();
    for (size_t i = 0; i < n; ++i) {
        int next = sequence[i];
        for (int k = 1; k <= order_; ++k) {
            if (i < static_cast<size_t>(k)) break;
            std::vector<int> hist;
            hist.reserve(k);
            for (size_t j = i - k; j < i; ++j) hist.push_back(sequence[j]);
            transitions_[hist][next] += 1;
        }
    }
}

void MarkovModel::trainMany(const std::vector<std::vector<int>>& sequences) {
    for (const auto &s : sequences) train(s);
}

std::unordered_map<int, uint32_t> MarkovModel::findWithBackoff(const std::vector<int>& history) const {
    for (int k = std::min<int>(order_, static_cast<int>(history.size())); k >= 1; --k) {
        std::vector<int> tail(history.end() - k, history.end());
        auto it = transitions_.find(tail);
        if (it != transitions_.end()) return it->second;
    }
    if (!unigramCounts_.empty()) return unigramCounts_;
    return {};
}

std::unordered_map<int, uint32_t> MarkovModel::getCountsForHistory(const std::vector<int>& history) const {
    return findWithBackoff(history);
}

int MarkovModel::sampleNext(const std::vector<int>& history, double temperature) const {
    auto counts = findWithBackoff(history);

    if (counts.empty()) {
        if (unigramCounts_.empty()) return 0;
        counts = unigramCounts_;
    }

    if (temperature <= 0.0) {
        int bestTok = 0;
        uint32_t bestCnt = 0;
        for (auto &p : counts) {
            if (p.second > bestCnt) {
                bestCnt = p.second;
                bestTok = p.first;
            }
        }
        return bestTok;
    }

    std::vector<std::pair<int, double>> items;
    items.reserve(counts.size());
    double total = 0.0;
    for (auto &kv : counts) {
        double w = std::pow(static_cast<double>(kv.second), 1.0 / temperature);
        items.emplace_back(kv.first, w);
        total += w;
    }

    if (total <= 0.0) {
        int bestTok = items.front().first;
        uint32_t bestCnt = 0;
        for (auto &p : counts) {
            if (p.second > bestCnt) {
                bestCnt = p.second;
                bestTok = p.first;
            }
        }
        return bestTok;
    }

    std::uniform_real_distribution<double> dist(0.0, total);
    double r = dist(rng_);
    double acc = 0.0;
    for (auto &it : items) {
        acc += it.second;
        if (r <= acc) return it.first;
    }
    return items.back().first;
}

size_t MarkovModel::vocabularySize() const {
    return unigramCounts_.size();
}
