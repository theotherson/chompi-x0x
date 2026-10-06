// 16-bit stereo WAV writer for the desktop renders.
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

inline void WriteWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, uint32_t sr)
{
    FILE* f = fopen(path.c_str(), "wb");
    if(!f)
    {
        perror(path.c_str());
        exit(1);
    }
    const uint32_t n = static_cast<uint32_t>(l.size()), data = n * 4, br = sr * 4, riff = 36 + data, fmt = 16;
    const uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&sr, 4, 1, f), fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
    for(uint32_t i = 0; i < n; i++)
    {
        auto c = [](float v) { return static_cast<int16_t>((v > 1.f ? 1.f : v < -1.f ? -1.f : v) * 32767.f); };
        int16_t s[2] = {c(l[i]), c(r[i])};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}
