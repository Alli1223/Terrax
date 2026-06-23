#include "screenshot.h"
#include "gl_loader.h"

#include <cstdio>
#include <cstdint>
#include <vector>
#include <ctime>
#include <filesystem>

namespace {

// --- CRC-32 (PNG chunk checksum) -------------------------------------------
uint32_t crcTable[256];
bool     crcReady = false;
void initCrc() {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
    crcReady = true;
}
uint32_t crc32(const uint8_t* d, size_t len) {
    if (!crcReady) initCrc();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = crcTable[(c ^ d[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// --- Adler-32 (zlib stream checksum) ---------------------------------------
uint32_t adler32(const uint8_t* d, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) { a = (a + d[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

void put32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)x);
}

void writeChunk(FILE* f, const char type[4], const std::vector<uint8_t>& data) {
    uint8_t lenb[4] = { (uint8_t)(data.size() >> 24), (uint8_t)(data.size() >> 16),
                        (uint8_t)(data.size() >> 8),  (uint8_t)data.size() };
    std::fwrite(lenb, 1, 4, f);
    std::fwrite(type, 1, 4, f);
    if (!data.empty()) std::fwrite(data.data(), 1, data.size(), f);
    std::vector<uint8_t> crcBuf;
    crcBuf.insert(crcBuf.end(), type, type + 4);
    crcBuf.insert(crcBuf.end(), data.begin(), data.end());
    uint8_t crcb[4]; uint32_t c = crc32(crcBuf.data(), crcBuf.size());
    crcb[0] = (uint8_t)(c >> 24); crcb[1] = (uint8_t)(c >> 16);
    crcb[2] = (uint8_t)(c >> 8);  crcb[3] = (uint8_t)c;
    std::fwrite(crcb, 1, 4, f);
}

// zlib stream wrapping `raw` in uncompressed (stored) DEFLATE blocks.
std::vector<uint8_t> zlibStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);           // zlib header (no compression)
    size_t off = 0;
    while (off < raw.size() || raw.empty()) {
        size_t n = raw.size() - off;
        if (n > 65535) n = 65535;
        bool final = (off + n >= raw.size());
        z.push_back(final ? 1 : 0);                 // BFINAL, BTYPE=00 (stored)
        z.push_back((uint8_t)(n & 0xff)); z.push_back((uint8_t)((n >> 8) & 0xff));
        uint16_t nlen = (uint16_t)~n;
        z.push_back((uint8_t)(nlen & 0xff)); z.push_back((uint8_t)((nlen >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
        if (final) break;
    }
    put32be(z, adler32(raw.data(), raw.size()));
    return z;
}

}  // namespace

bool writePNG(const std::string& path, int width, int height, const unsigned char* rgb) {
    if (width <= 0 || height <= 0 || !rgb) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    std::fwrite(sig, 1, 8, f);

    std::vector<uint8_t> ihdr;
    put32be(ihdr, (uint32_t)width);
    put32be(ihdr, (uint32_t)height);
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(2);   // colour type: truecolour RGB
    ihdr.push_back(0);   // compression
    ihdr.push_back(0);   // filter
    ihdr.push_back(0);   // interlace
    writeChunk(f, "IHDR", ihdr);

    // Filtered scanlines: each row prefixed with filter byte 0 (None).
    std::vector<uint8_t> raw;
    raw.reserve((size_t)height * (1 + (size_t)width * 3));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const unsigned char* row = rgb + (size_t)y * width * 3;
        raw.insert(raw.end(), row, row + (size_t)width * 3);
    }
    writeChunk(f, "IDAT", zlibStored(raw));
    writeChunk(f, "IEND", {});

    std::fclose(f);
    return true;
}

std::string saveFramebufferPNG(int width, int height, const std::string& tag) {
    if (width <= 0 || height <= 0) return "";
    std::vector<unsigned char> buf((size_t)width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, buf.data());

    // glReadPixels gives bottom-up rows; flip to top-down for the PNG.
    std::vector<unsigned char> flipped((size_t)width * height * 3);
    for (int y = 0; y < height; ++y) {
        const unsigned char* src = buf.data() + (size_t)(height - 1 - y) * width * 3;
        std::copy(src, src + (size_t)width * 3, flipped.data() + (size_t)y * width * 3);
    }

    std::error_code ec;
    std::filesystem::create_directories("screenshots", ec);
    char name[128];
    std::time_t t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    std::snprintf(name, sizeof(name), "screenshots/terrax_%04d%02d%02d_%02d%02d%02d%s%s.png",
                  lt ? lt->tm_year + 1900 : 0, lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
                  lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0,
                  tag.empty() ? "" : "_", tag.c_str());
    if (!writePNG(name, width, height, flipped.data())) return "";
    return name;
}
