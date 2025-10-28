#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

enum class TransformSmoothingMethod {
    None = 0,
    Lerp = 1,
    CriticallyDampedSpring = 2
};

struct TransformSmoothingParams {
    TransformSmoothingMethod method = TransformSmoothingMethod::None;
    float lerp_rate = 0.0f;
    float spring_frequency = 0.0f;
    float max_step = 0.0f;
    float snap_threshold = 0.0f;
};

struct TransformSmoothingState {
    float prev = 0.0f;
    float target = 0.0f;
    float current = 0.0f;
    float velocity = 0.0f;
    TransformSmoothingParams params{};

    void set_params(const TransformSmoothingParams& p) { params = p; }

    void reset(float value) {
        prev     = value;
        target   = value;
        current  = value;
        velocity = 0.0f;
    }

    float value_for_render() const {
        const float snap = std::max(0.0f, params.snap_threshold);
        if (std::fabs(target - current) <= snap) {
            return target;
        }
        return current;
    }

    void advance(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f) {
            prev     = current;
            current  = target;
            velocity = 0.0f;
            return;
        }

        prev = current;

        const float snap = std::max(0.0f, params.snap_threshold);
        const float delta = target - current;
        if (std::fabs(delta) <= snap) {
            current  = target;
            velocity = 0.0f;
            return;
        }

        switch (params.method) {
        case TransformSmoothingMethod::None:
            current  = target;
            velocity = 0.0f;
            break;
        case TransformSmoothingMethod::Lerp: {
            const float rate = std::max(0.0f, params.lerp_rate);
            float factor = (rate <= 0.0f) ? 1.0f : (1.0f - std::exp(-rate * dt));
            factor       = std::clamp(factor, 0.0f, 1.0f);
            float step   = delta * factor;
            if (params.max_step > 0.0f && std::isfinite(params.max_step)) {
                const float max_delta = params.max_step * dt;
                step = std::clamp(step, -max_delta, max_delta);
            }
            current += step;
            velocity = step / dt;
            break;
        }
        case TransformSmoothingMethod::CriticallyDampedSpring: {
            float smooth_time = 0.0f;
            if (std::isfinite(params.spring_frequency) && params.spring_frequency > 1e-4f) {
                smooth_time = 1.0f / params.spring_frequency;
            }
            smooth_time = std::max(smooth_time, 1e-4f);
            const float omega = 2.0f / smooth_time;
            float change      = current - target;
            if (params.max_step > 0.0f && std::isfinite(params.max_step)) {
                const float max_change = params.max_step * smooth_time;
                change = std::clamp(change, -max_change, max_change);
            }
            const float adjusted_target = current - change;
            const float x               = omega * dt;
            const float exp             = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
            const float temp            = (velocity + omega * change) * dt;
            velocity                    = (velocity - omega * temp) * exp;
            float output                = adjusted_target + (change + temp) * exp;
            if ((target - current > 0.0f) == (output > target)) {
                output   = target;
                velocity = (output - target) / dt;
            }
            current = output;
            break;
        }
        }

        if (!std::isfinite(current)) {
            current  = target;
            velocity = 0.0f;
        }

        if (std::fabs(target - current) <= snap) {
            current  = target;
            velocity = 0.0f;
        }
    }
};

