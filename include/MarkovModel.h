#pragma once
#include <vector>
#include <unordered_map>
#include <random>

class MarkovModel {
public:
    explicit MarkovModel(int order = 2);
    void train(const std::vector<int>& sequence);
    void trainMany(const std::vector<std::vector<int>>& sequences);
    int sampleNext(const std::vector<int>& history, double temperature = 1.0) const;
    std::unordered_map<int, uint32_t> getCountsForHistory(const std::vector<int>& history) const;
    size_t vocabularySize() const;

private:
    int order_;
    struct VecHash {
        size_t operator()(const std::vector<int>& v) const noexcept {
            size_t h = 1469598103934665603ULL;
            for (int x : v) {
                h ^= static_cast<size_t>(x) + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2);
            }
            return h;
        }
    };
    std::unordered_map<std::vector<int>, std::unordered_map<int, uint32_t>, VecHash> transitions_;
    std::unordered_map<int, uint32_t> unigramCounts_;
    mutable std::mt19937 rng_;
    std::unordered_map<int, uint32_t> findWithBackoff(const std::vector<int>& history) const;
};
