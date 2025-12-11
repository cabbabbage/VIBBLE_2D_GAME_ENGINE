#include "render/light_flicker.hpp"

#include <algorithm>
#include <cmath>

float LightFlickerCalculator::compute_multiplier(const LightSource& light, float time_seconds) {
    const float speed_setting =
        std::clamp(static_cast<float>(light.flicker_speed), 0.0f, 100.0f) / 100.0f;
    const float smooth_setting =
        std::clamp(static_cast<float>(light.flicker_smoothness), 0.0f, 100.0f) / 100.0f;

    if (speed_setting <= 0.001f) {
        return 1.0f;
    }

    auto mix = [](std::uint32_t seed, std::uint32_t value) {
        seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
};

    auto to_rand = [](std::uint32_t h) {
        return static_cast<float>(h & 0xFFFFu) / 32767.5f - 1.0f;
};

    auto value_noise_1d = [&](float t, std::uint32_t seed) {
        if (!std::isfinite(t)) {
            return 0.0f;
        }
        const int   i   = static_cast<int>(std::floor(t));
        const float f   = t - static_cast<float>(i);
        const float f2  = f * f;
        const float f3  = f2 * f;
        const float u   = f3 * (f * (f * 6.0f - 15.0f) + 10.0f);
        const float a   = to_rand(mix(seed, static_cast<std::uint32_t>(i)));
        const float b   = to_rand(mix(seed, static_cast<std::uint32_t>(i + 1)));
        return a + (b - a) * u;
};

    std::uint32_t base = 0x811C9DC5u;
    base = mix(base, static_cast<std::uint32_t>(light.offset_x));
    base = mix(base, static_cast<std::uint32_t>(light.offset_y));
    base = mix(base, static_cast<std::uint32_t>(light.radius));
    base = mix(base, static_cast<std::uint32_t>(light.intensity));
    base = mix(base, static_cast<std::uint32_t>(light.fall_off));
    base = mix(base, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(light.texture) & 0xFFFFu));

    const float base_rate = 0.4f + 6.0f * speed_setting;

    const float f0 = base_rate * 1.00f;
    const float f1 = base_rate * 2.17f;
    const float f2 = base_rate * 3.73f;
    const std::uint32_t s0 = mix(base, 0xA1B2C3D4u);
    const std::uint32_t s1 = mix(base, 0xBEEF1234u);
    const std::uint32_t s2 = mix(base, 0xDEADBEEFu);

    float w0 = 0.6f + 0.3f * smooth_setting;
    float w1 = 0.3f * (1.0f - 0.5f * smooth_setting);
    float w2 = 0.1f * (1.0f - smooth_setting);
    float wsum = std::max(1e-6f, w0 + w1 + w2);
    w0 /= wsum;
    w1 /= wsum;
    w2 /= wsum;

    const float t = std::isfinite(time_seconds) ? time_seconds : 0.0f;
    float n0 = value_noise_1d(t * f0, s0);
    float n1 = value_noise_1d(t * f1, s1);
    float n2 = value_noise_1d(t * f2, s2);
    float noise = w0 * n0 + w1 * n1 + w2 * n2;

    if (smooth_setting < 0.5f) {
        const float jitter_rate = 70.0f + 260.0f * speed_setting;
        const float jt = t * jitter_rate + static_cast<float>((base >> 8) & 0xFFu) * 0.013f;
        const int   ji = static_cast<int>(std::floor(jt));
        const float jf = jt - static_cast<float>(ji);
        const float u  = jf * jf * (3.0f - 2.0f * jf);
        const float ja = to_rand(mix(base, static_cast<std::uint32_t>(ji)));
        const float jb = to_rand(mix(base, static_cast<std::uint32_t>(ji + 1)));
        const float j  = ja + (jb - ja) * u;
        const float jitter_amp = (0.1f + 0.15f * speed_setting) * (1.0f - smooth_setting);
        noise = std::clamp(noise * (1.0f - jitter_amp) + j * jitter_amp, -1.0f, 1.0f);
    }

    const float amplitude  = 0.12f + 0.45f * speed_setting;
    const float multiplier = 1.0f + std::clamp(noise, -1.0f, 1.0f) * amplitude;
    return std::clamp(multiplier, 0.2f, 1.0f + amplitude);
}
