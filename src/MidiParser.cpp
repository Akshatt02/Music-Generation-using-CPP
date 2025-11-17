#include "midiparser.h"
#include "Utils.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <deque>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace {

struct RawEvent {
    int pitch;
    uint64_t tick;
    bool on;
    int track;
    uint64_t seq;
};

struct TempoEvent {
    uint64_t tick;
    uint32_t microsecondsPerQuarter;
};

static int readByte(std::ifstream &f) {
    int c = f.get();
    if (c == EOF) return -1;
    return c & 0xFF;
}

}

std::vector<NoteEvent> Parser::parseMidiFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Parser::parseMidiFile: failed to open MIDI file: " << path << '\n';
        return {};
    }

    char hdr[4] = {0};
    file.read(hdr, 4);
    if (!file.good()) {
        std::cerr << "Parser::parseMidiFile: failed reading header id\n";
        return {};
    }
    if (std::string(hdr, 4) != "MThd") {
        std::cerr << "Parser::parseMidiFile: not a MIDI file (MThd missing)\n";
        return {};
    }

    uint32_t headerSize = Utils::readBE32(file);
    uint16_t format = Utils::readBE16(file);
    uint16_t nTracks = Utils::readBE16(file);
    uint16_t division = Utils::readBE16(file);

    if (!file.good()) {
        std::cerr << "Parser::parseMidiFile: truncated header\n";
        return {};
    }

    if (headerSize > 6) file.seekg(headerSize - 6, std::ios::cur);

    bool isSMPTE = (division & 0x8000) != 0;
    int PPQ = 480;
    if (!isSMPTE) {
        PPQ = division;
        if (PPQ == 0) {
            std::cerr << "Parser::parseMidiFile: division/PPQ is zero, falling back to 480\n";
            PPQ = 480;
        }
    } else {
        std::cerr << "Parser::parseMidiFile: SMPTE time division detected - not fully supported. Using fallback PPQ=480\n";
        PPQ = 480;
    }

    std::vector<RawEvent> rawEvents;
    std::vector<TempoEvent> tempoEvents;
    uint64_t globalSeq = 0;

    for (int trackIndex = 0; trackIndex < nTracks; ++trackIndex) {
        uint64_t absoluteTick = 0;

        char chunkId[4] = {0};
        file.read(chunkId, 4);
        if (!file.good()) {
            std::cerr << "Parser::parseMidiFile: unexpected EOF while reading track header\n";
            break;
        }

        uint32_t trackSize = Utils::readBE32(file);
        std::streampos trackEnd = file.tellg() + std::streamoff(trackSize);

        if (std::string(chunkId,4) != "MTrk") {
            std::cerr << "Parser::parseMidiFile: warning - expected MTrk chunk, got '" << std::string(chunkId,4) << "'\n";
        }

        unsigned char runningStatus = 0;

        while (file.good() && file.tellg() < trackEnd) {
            uint32_t delta = Utils::readVarLen(file);
            absoluteTick += delta;

            int first = readByte(file);
            if (first < 0) {
                break;
            }

            unsigned char status;
            int dataByte1 = -1;

            if (first & 0x80) {
                // status byte
                status = static_cast<unsigned char>(first);
                runningStatus = status;
            } else {
                if (runningStatus == 0) {
                    std::cerr << "Parser::parseMidiFile: running status used before any status byte; skipping remaining of track\n";
                    break;
                }
                status = runningStatus;
                dataByte1 = first;
            }

            if (status == 0xFF) {
                // Meta event
                int metaType = readByte(file);
                if (metaType < 0) break;
                uint32_t len = Utils::readVarLen(file);
                if (metaType == 0x51 && len == 3) {
                    int b1 = readByte(file);
                    int b2 = readByte(file);
                    int b3 = readByte(file);
                    if (b1 < 0 || b2 < 0 || b3 < 0) break;
                    uint32_t micro = (static_cast<uint32_t>(b1) << 16) | (static_cast<uint32_t>(b2) << 8) | static_cast<uint32_t>(b3);
                    tempoEvents.push_back({ absoluteTick, micro });
                } else {
                    file.seekg((std::streamoff)len, std::ios::cur);
                }
                continue;
            }

            if (status == 0xF0 || status == 0xF7) {
                uint32_t len = Utils::readVarLen(file);
                file.seekg((std::streamoff)len, std::ios::cur);
                continue;
            }

            unsigned char eventType = status & 0xF0;

            auto readData = [&](int already)->int {
                if (already >= 0) return already;
                return readByte(file);
            };

            if (eventType == 0x90 || eventType == 0x80) {
                int pitch = readData(dataByte1);
                int velocity = readData(-1);
                if (pitch < 0 || velocity < 0) break;

                bool isNoteOn = (eventType == 0x90 && velocity > 0);
                rawEvents.push_back({ pitch, absoluteTick, isNoteOn, trackIndex, globalSeq++ });
            } else if (eventType == 0xC0 || eventType == 0xD0) {
                (void)readData(dataByte1);
            } else {
                (void)readData(dataByte1);
                (void)readData(-1);
            }
        }

        if (file.tellg() < trackEnd) {
            file.seekg(trackEnd);
        }
    }

    std::sort(rawEvents.begin(), rawEvents.end(), [](const RawEvent &a, const RawEvent &b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.track != b.track) return a.track < b.track;
        return a.seq < b.seq;
    });

    std::map<int, std::deque<uint64_t>> active;
    struct TempNote { int pitch; uint64_t startTick; uint64_t durTicks; };
    std::vector<TempNote> tempNotes;
    tempNotes.reserve(rawEvents.size() / 2);

    for (const auto &e : rawEvents) {
        if (e.on) {
            active[e.pitch].push_back(e.tick);
        } else {
            auto &dq = active[e.pitch];
            if (!dq.empty()) {
                uint64_t s = dq.front(); dq.pop_front();
                uint64_t dur = (e.tick > s) ? (e.tick - s) : 0;
                tempNotes.push_back({ e.pitch, s, dur });
            } else {
                
            }
        }
    }
    std::sort(tempoEvents.begin(), tempoEvents.end(), [](const TempoEvent &a, const TempoEvent &b){ return a.tick < b.tick; });

    struct Segment { uint64_t tickStart; uint32_t microPerQuarter; };
    std::vector<Segment> segments;
    segments.reserve(tempoEvents.size() + 1);

    uint32_t defaultMicro = 500000;
    uint64_t prevTick = 0;
    uint32_t currMicro = defaultMicro;

    for (const auto &te : tempoEvents) {
        if (te.tick > prevTick) {
            segments.push_back({ prevTick, currMicro });
            prevTick = te.tick;
        }
        currMicro = te.microsecondsPerQuarter;
    }
    segments.push_back({ prevTick, currMicro });

    std::vector<double> prefixSeconds; prefixSeconds.reserve(segments.size()+1);
    prefixSeconds.push_back(0.0);
    for (size_t i = 0; i < segments.size(); ++i) {
        uint64_t t0 = segments[i].tickStart;
        uint64_t t1 = (i + 1 < segments.size()) ? segments[i+1].tickStart : std::numeric_limits<uint64_t>::max();
        double micro = static_cast<double>(segments[i].microPerQuarter);
        double secondsInSegment = 0.0;
        if (t1 != std::numeric_limits<uint64_t>::max()) {
            uint64_t dt = t1 - t0;
            secondsInSegment = (static_cast<double>(dt) * micro) / (1e6 * static_cast<double>(PPQ));
        }
        prefixSeconds.push_back(prefixSeconds.back() + secondsInSegment);
    }

    auto ticksToSecondsAt = [&](uint64_t tick) -> double {
        size_t lo = 0, hi = segments.size();
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (segments[mid].tickStart <= tick) lo = mid;
            else hi = mid;
        }
        uint64_t segTickStart = segments[lo].tickStart;
        double micro = static_cast<double>(segments[lo].microPerQuarter);
        double baseSec = prefixSeconds[lo];
        uint64_t dt = tick - segTickStart;
        double extra = (static_cast<double>(dt) * micro) / (1e6 * static_cast<double>(PPQ));
        return baseSec + extra;
    };

    std::vector<NoteEvent> out;
    out.reserve(tempNotes.size());
    for (const auto &tn : tempNotes) {
        NoteEvent ne;
        ne.pitch = tn.pitch;
        ne.startTime = ticksToSecondsAt(tn.startTick);
        double endSec = ticksToSecondsAt(tn.startTick + tn.durTicks);
        ne.duration = endSec - ne.startTime;
        out.push_back(ne);
    }

    return out;
}

std::vector<int> Parser::parseMelodyTxt(const std::string& path) {
    std::ifstream in(path);
    std::vector<int> seq;
    if (!in.is_open()) return seq;
    int v;
    while (in >> v) seq.push_back(v);
    return seq;
}

std::vector<double> Parser::parseDurationTxt(const std::string& path) {
    std::ifstream in(path);
    std::vector<double> seq;
    if (!in.is_open()) return seq;
    double v;
    while (in >> v) seq.push_back(v);
    return seq;
}

void Parser::exportMelodyTxt(const std::vector<NoteEvent>& notes, const std::string& outPath) {
    std::ofstream out(outPath);
    if (!out.is_open()) {
        std::cerr << "Parser::exportMelodyTxt: Failed to write melody file: " << outPath << '\n';
        return;
    }
    for (const auto &n : notes) out << n.pitch << " ";
}

void Parser::exportDurationTxt(const std::vector<NoteEvent>& notes, const std::string& outPath) {
    std::ofstream out(outPath);
    if (!out.is_open()) {
        std::cerr << "Parser::exportDurationTxt: Failed to write duration file: " << outPath << '\n';
        return;
    }
    for (const auto &n : notes) out << n.duration << " ";
}
