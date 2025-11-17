#include "Utils.h"

uint16_t Utils::readBE16(std::ifstream& file) {
    unsigned char b1 = file.get();
    unsigned char b2 = file.get();
    return (b1 << 8) | b2;
}

uint32_t Utils::readBE32(std::ifstream& file) {
    unsigned char b1 = file.get();
    unsigned char b2 = file.get();
    unsigned char b3 = file.get();
    unsigned char b4 = file.get();
    return (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
}

uint32_t Utils::readVarLen(std::ifstream& file) {
    uint32_t value = 0;
    unsigned char c;
    do {
        c = file.get();
        value = (value << 7) | (c & 0x7F);
    } while (c & 0x80);
    return value;
}
