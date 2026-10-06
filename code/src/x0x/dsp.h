/** @file dsp.h
 *  @brief Small math helpers for the x0x DSP. Pure C++, no libDaisy, so the
 *  whole x0x core also builds on the desktop (see host/).
 */
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace x0x
{

using std::size_t;

constexpr float kPi = 3.14159265358979f;

inline float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

inline int ClampInt(int x, int lo, int hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/** 2^x, accurate to ~0.01 % over the range used. Cheap enough per sample. */
inline float FastExp2(float x)
{
    x             = Clamp(x, -30.f, 30.f);
    const float fl = floorf(x);
    const float f  = x - fl;
    const float p  = 1.f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    return ldexpf(p, static_cast<int>(fl));
}

/** Pade tanh, clamped where it would turn back. */
inline float FastTanh(float x)
{
    if(x > 3.f)
        return 1.f;
    if(x < -3.f)
        return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

/** Clean below 0.75, then bends smoothly towards 1.0: keeps the output in
 *  range however hard the effects and drive are pushed. */
inline float SoftLimit(float x)
{
    const float a = fabsf(x);
    if(a <= 0.75f)
        return x;
    const float over = (a - 0.75f) * 4.f;
    const float y    = 0.75f + 0.25f * (over / (1.f + over));
    return x < 0.f ? -y : y;
}

inline float MidiToHz(float note)
{
    return 440.f * FastExp2((note - 69.f) * (1.f / 12.f));
}

/** Per-sample coefficient for a one-pole that moves ~63 % of the way in
 *  `seconds`. */
inline float TauToCoef(float seconds, float sr)
{
    if(seconds <= 0.f)
        return 1.f;
    return 1.f - expf(-1.f / (seconds * sr));
}

/** Knob 0..1 to a value exponentially between lo and hi. */
inline float KnobToExp(float v, float lo, float hi)
{
    return lo * powf(hi / lo, v);
}

} // namespace x0x
