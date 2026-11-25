// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <cmath>

#include "VideoCommon/VR/FrameUI/geometry.h"
#include "VideoCommon/VR/FrameUI/toast_corner.h"

namespace FrameXr::Vis {

using Input::Posef;
using Input::Quatf;
using Input::Vec3f;

bool PointInView(const EyeFov& eye, const Vec3f& point, float margin_deg) {
    const Vec3f p = Input::Transform(Input::Inverse(eye.pose), point);
    if (p.z > -0.05f) {
        return false; // behind, or at the eye
    }
    const float h = std::atan2(p.x, -p.z);
    const float v = std::atan2(p.y, -p.z);
    const float m = margin_deg * 3.14159265f / 180.0f;
    return h >= eye.left + m && h <= eye.right - m && v >= eye.down + m && v <= eye.up - m;
}

namespace Toast {

namespace {
/// The toast this far in front of the screen's surface (metres), so it is never behind it.
constexpr float kLift = 0.003f;
} // namespace

float CurveLift(float toast_width, float curve_radius) {
    if (curve_radius <= 0.0f) {
        return kLift;
    }
    return toast_width * toast_width / (8.0f * curve_radius) + kCurveClear;
}

Placed OnScreen(const Posef& screen, float width, float height, float curve_radius, float aspect,
                float share) {
    Placed out;
    const float inset = height * kScreenInset;
    out.height = height * share;
    out.width = out.height * aspect;
    const float room = width - 2.0f * inset;
    if (out.width > room && room > 0.0f) {
        const float s = room / out.width;
        out.width *= s;
        out.height *= s;
    }
    // In the screen's own axes, from its centre.
    const float x = width * 0.5f - inset - out.width * 0.5f;
    const float y = height * 0.5f - inset - out.height * 0.5f;
    Vec3f local{x, y, kLift};
    Quatf turn{};
    if (curve_radius > 0.0f) {
        // On the cylinder (geometry.h CylinderFor): its axis the radius in front of the
        // screen's centre, the arc as long as the screen is wide; the toast faces the axis.
        // The toast is flat, so its ends stand w^2/(8r) farther from the axis
        // than its centre: lifted by that and kCurveClear, all of it is in front of the curve.
        const float phi = x / curve_radius;
        const float r = curve_radius - CurveLift(out.width, curve_radius);
        local = {r * std::sin(phi), y, curve_radius - r * std::cos(phi)};
        turn = {0.0f, std::sin(-phi * 0.5f), 0.0f, std::cos(-phi * 0.5f)};
    }
    out.pose.orientation = Input::Normalized(Input::Multiply(screen.orientation, turn));
    out.pose.position = Input::Transform(screen, local);
    return out;
}

bool InView(const std::array<EyeFov, 2>& eyes, const Placed& p) {
    for (const EyeFov& eye : eyes) {
        for (const float sx : {-0.5f, 0.5f}) {
            for (const float sy : {-0.5f, 0.5f}) {
                const Vec3f c = Input::Transform(p.pose, {sx * p.width, sy * p.height, 0});
                if (!PointInView(eye, c, kInViewMarginDeg)) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace Toast
} // namespace FrameXr::Vis
