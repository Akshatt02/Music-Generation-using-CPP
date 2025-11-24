#include "MidiWriter.h"
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>
#include <cassert>
#include <cmath>

static void writeBE16(std::ofstream &f, uint16_t v) {
    unsigned char b1 = (v >> 8) & 0xFF;
    unsigned char b2 = v & 0xFF;
    f.put(b1);
    f.put(b2);
}
static void writeBE32(std::ofstream &f, uint32_t v) {
    unsigned char b1 = (v >> 24) & 0xFF;
    unsigned char b2 = (v >> 16) & 0xFF;
    unsigned char b3 = (v >> 8) & 0xFF;
    unsigned char b4 = v & 0xFF;
    f.put(b1); f.put(b2); f.put(b3); f.put(b4);
}

static void writeVarLen(std::ofstream &f, uint32_t value) {
    unsigned char buffer[5];
    int idx = 0;
    buffer[idx++] = value & 0x7F;
    value >>= 7;
    while (value) {
        buffer[idx++] = 0x80 | (value & 0x7F);
        value >>= 7;
    }
    for (int i = idx - 1; i >= 0; --i) f.put(buffer[i]);
}

struct Event {
    uint64_t tick;
    std::vector<unsigned char> bytes;
    bool isMeta = false;
    int metaPriority = 0;
};

bool MidiWriter::write(const std::string& outPath, const std::vector<NoteEvent>& notes, int ppq, uint32_t microsecondsPerQuarter, int channel, int velocity) const {
    if (ppq <= 0) ppq = 480;
    if (channel < 0 || channel > 15) channel = 0;
    if (velocity < 0) velocity = 64;
    if (velocity > 127) velocity = 127;
    auto secToTicks = [&](double s)->uint64_t {
        double v = s * 1'000'000.0 * static_cast<double>(ppq) / static_cast<double>(microsecondsPerQuarter);
        if (v < 0.0) v = 0.0;
        return static_cast<uint64_t>(std::llround(v));
    };

    std::vector<Event> events;
    events.reserve(notes.size() * 2 + 2);

    {
        Event e;
        e.tick = 0;
        e.isMeta = true;
        e.metaPriority = 0;
        e.bytes.push_back(0xFF);
        e.bytes.push_back(0x51);
        e.bytes.push_back(0x03);
        e.bytes.push_back((microsecondsPerQuarter >> 16) & 0xFF);
        e.bytes.push_back((microsecondsPerQuarter >> 8) & 0xFF);
        e.bytes.push_back(microsecondsPerQuarter & 0xFF);
        events.push_back(std::move(e));
    }

    for (const auto &n : notes) {
        int pitch = n.pitch;
        uint64_t onTick = secToTicks(n.startTime);
        uint64_t offTick = secToTicks(n.startTime + n.duration);
        if (offTick < onTick) offTick = onTick;

        Event onEv;
        onEv.tick = onTick;
        onEv.isMeta = false;
        unsigned char statusOn = static_cast<unsigned char>(0x90 | (channel & 0x0F));
        onEv.bytes.push_back(statusOn);
        onEv.bytes.push_back(static_cast<unsigned char>(pitch & 0x7F));
        onEv.bytes.push_back(static_cast<unsigned char>(velocity & 0x7F));
        events.push_back(std::move(onEv));

        Event offEv;
        offEv.tick = offTick;
        offEv.isMeta = false;
        unsigned char statusOff = static_cast<unsigned char>(0x80 | (channel & 0x0F));
        offEv.bytes.push_back(statusOff);
        offEv.bytes.push_back(static_cast<unsigned char>(pitch & 0x7F));
        offEv.bytes.push_back(static_cast<unsigned char>(0));
        events.push_back(std::move(offEv));
    }

    std::sort(events.begin(), events.end(), [](const Event &a, const Event &b){
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.isMeta != b.isMeta) return a.isMeta;
        if (a.isMeta && b.isMeta) return a.metaPriority < b.metaPriority;
        return false;
    });

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "MidiWriter::write: failed to open " << outPath << " for writing\n";
        return false;
    }

    out.write("MThd", 4);
    writeBE32(out, 6);
    writeBE16(out, 0);
    writeBE16(out, 1);
    writeBE16(out, static_cast<uint16_t>(ppq));

    std::vector<unsigned char> trackData;
    trackData.reserve(events.size() * 16);

    uint64_t prevTick = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        const Event &ev = events[i];
        uint64_t deltaTicks = ev.tick - prevTick;
        {
            unsigned char buffer[5];
            int idx = 0;
            uint32_t v = static_cast<uint32_t>(deltaTicks);
            buffer[idx++] = v & 0x7F;
            v >>= 7;
            while (v) {
                buffer[idx++] = 0x80 | (v & 0x7F);
                v >>= 7;
            }
            for (int j = idx - 1; j >= 0; --j) trackData.push_back(buffer[j]);
        }
        for (unsigned char b : ev.bytes) trackData.push_back(b);

        prevTick = ev.tick;
    }

    trackData.push_back(0x00);
    trackData.push_back(0xFF);
    trackData.push_back(0x2F);
    trackData.push_back(0x00);

    out.write("MTrk", 4);
    writeBE32(out, static_cast<uint32_t>(trackData.size()));
    out.write(reinterpret_cast<const char*>(trackData.data()), trackData.size());

    out.close();
    return true;
}
