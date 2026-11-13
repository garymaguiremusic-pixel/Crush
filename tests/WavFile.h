#pragma once

// Minimal WAV reader/writer for the test harness (PCM 16/24/32-bit and
// IEEE float 32/64-bit in; float 32-bit out). No dependencies.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wav
{

struct Audio
{
    int sampleRate = 44100;
    std::vector<float> left, right;   // mono files are duplicated into both
};

inline uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

inline bool read (const std::string& path, Audio& out, std::string& error)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr) { error = "cannot open " + path; return false; }
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t got;
    while ((got = std::fread (buf, 1, sizeof (buf), f)) > 0)
        data.insert (data.end(), buf, buf + got);
    std::fclose (f);

    if (data.size() < 12 || std::memcmp (data.data(), "RIFF", 4) != 0 || std::memcmp (data.data() + 8, "WAVE", 4) != 0)
    { error = "not a RIFF/WAVE file"; return false; }

    int format = 0, channels = 0, bits = 0;
    const uint8_t* samples = nullptr;
    size_t sampleBytes = 0;

    for (size_t pos = 12; pos + 8 <= data.size(); )
    {
        const uint8_t* chunk = data.data() + pos;
        const uint32_t size = rd32 (chunk + 4);
        const uint8_t* body = chunk + 8;
        const size_t avail = data.size() - pos - 8;

        if (std::memcmp (chunk, "fmt ", 4) == 0 && size >= 16)
        {
            format = rd16 (body);
            channels = rd16 (body + 2);
            out.sampleRate = (int) rd32 (body + 4);
            bits = rd16 (body + 14);
            if (format == 0xFFFE && size >= 26)
                format = rd16 (body + 24);   // WAVE_FORMAT_EXTENSIBLE sub-format
        }
        else if (std::memcmp (chunk, "data", 4) == 0)
        {
            samples = body;
            sampleBytes = size < avail ? size : avail;
        }
        pos += 8 + size + (size & 1);
    }

    if (samples == nullptr || channels < 1 || (format != 1 && format != 3))
    { error = "unsupported WAV format"; return false; }

    const int bytesPer = bits / 8;
    const size_t frames = sampleBytes / (size_t) (bytesPer * channels);
    out.left.resize (frames);
    out.right.resize (frames);

    auto sampleAt = [&] (size_t frame, int ch) -> float
    {
        const uint8_t* p = samples + (frame * (size_t) channels + (size_t) ch) * (size_t) bytesPer;
        if (format == 3 && bits == 32) { float v; std::memcpy (&v, p, 4); return v; }
        if (format == 3 && bits == 64) { double v; std::memcpy (&v, p, 8); return (float) v; }
        if (bits == 16) return (float) (int16_t) rd16 (p) / 32768.0f;
        if (bits == 24) { int32_t v = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) >> 8; return (float) v / 8388608.0f; }
        if (bits == 32) return (float) ((double) (int32_t) rd32 (p) / 2147483648.0);
        return 0.0f;
    };

    for (size_t i = 0; i < frames; ++i)
    {
        out.left[i] = sampleAt (i, 0);
        out.right[i] = sampleAt (i, channels > 1 ? 1 : 0);
    }
    return true;
}

inline bool write (const std::string& path, const Audio& in)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;

    const uint32_t frames = (uint32_t) in.left.size();
    const uint32_t dataBytes = frames * 2u * 4u;
    auto w32 = [f] (uint32_t v) { uint8_t b[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [f] (uint16_t v) { uint8_t b[2] = { (uint8_t) v, (uint8_t) (v >> 8) }; std::fwrite (b, 1, 2, f); };

    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (3); w16 (2);
    w32 ((uint32_t) in.sampleRate); w32 ((uint32_t) in.sampleRate * 8u); w16 (8); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
    {
        std::fwrite (&in.left[i], 4, 1, f);
        std::fwrite (&in.right[i], 4, 1, f);
    }
    std::fclose (f);
    return true;
}

} // namespace wav
