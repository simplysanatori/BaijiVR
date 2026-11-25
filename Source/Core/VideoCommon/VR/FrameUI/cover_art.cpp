// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoCommon/VR/FrameUI/cover_art.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace FrameXr::Menu {

namespace {

using Pixel = std::array<float, 4>;

Pixel At(const GameIcon& image, int x, int y) {
    x = std::clamp(x, 0, image.w - 1);
    y = std::clamp(y, 0, image.h - 1);
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.w) +
                           static_cast<std::size_t>(x)) * 4;
    return {static_cast<float>(image.rgba[i]), static_cast<float>(image.rgba[i + 1]),
            static_cast<float>(image.rgba[i + 2]), static_cast<float>(image.rgba[i + 3])};
}

/// The mean of the image over [x0, x1) x [y0, y1) (in its pixels), bilinear when that is less than
/// a pixel.
Pixel Mean(const GameIcon& image, float x0, float y0, float x1, float y1) {
    Pixel sum{};
    if (x1 - x0 <= 1.0f && y1 - y0 <= 1.0f) {
        const float x = (x0 + x1) * 0.5f - 0.5f, y = (y0 + y1) * 0.5f - 0.5f;
        const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
        const float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 2; i++) {
                const float w = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
                const Pixel p = At(image, ix + i, iy + j);
                for (std::size_t c = 0; c < 4; c++) {
                    sum[c] += p[c] * w;
                }
            }
        }
        return sum;
    }
    float total = 0.0f;
    for (int y = static_cast<int>(std::floor(y0)); y < static_cast<int>(std::ceil(y1)); y++) {
        const float h = std::min(y1, static_cast<float>(y + 1)) - std::max(y0, static_cast<float>(y));
        for (int x = static_cast<int>(std::floor(x0)); x < static_cast<int>(std::ceil(x1)); x++) {
            const float w =
                (std::min(x1, static_cast<float>(x + 1)) - std::max(x0, static_cast<float>(x))) * h;
            const Pixel p = At(image, x, y);
            for (std::size_t c = 0; c < 4; c++) {
                sum[c] += p[c] * w;
            }
            total += w;
        }
    }
    for (float& c : sum) {
        c /= std::max(total, 1e-6f);
    }
    return sum;
}

GameIcon Blank(int w, int h) {
    GameIcon out;
    out.w = w;
    out.h = h;
    out.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    return out;
}

void Put(GameIcon& image, int x, int y, const Pixel& p) {
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.w) +
                           static_cast<std::size_t>(x)) * 4;
    for (std::size_t c = 0; c < 4; c++) {
        image.rgba[i + c] = static_cast<uint8_t>(std::clamp(p[c] + 0.5f, 0.0f, 255.0f));
    }
}

/// `image` over `w` x `h`, the part of it that fills them in proportion.
GameIcon Filled(const GameIcon& image, int w, int h) {
    const float scale = std::max(static_cast<float>(w) / static_cast<float>(image.w),
                                 static_cast<float>(h) / static_cast<float>(image.h));
    const float x = (static_cast<float>(image.w) - static_cast<float>(w) / scale) * 0.5f;
    const float y = (static_cast<float>(image.h) - static_cast<float>(h) / scale) * 0.5f;
    GameIcon out = Blank(w, h);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            Put(out, i, j,
                Mean(image, x + static_cast<float>(i) / scale, y + static_cast<float>(j) / scale,
                     x + static_cast<float>(i + 1) / scale, y + static_cast<float>(j + 1) / scale));
        }
    }
    return out;
}

GameIcon Blurred(const GameIcon& image, int radius) {
    GameIcon out = image;
    for (int pass = 0; pass < 2; pass++) {
        const GameIcon from = out;
        for (int y = 0; y < out.h; y++) {
            for (int x = 0; x < out.w; x++) {
                Pixel sum{};
                for (int d = -radius; d <= radius; d++) {
                    const Pixel p = pass == 0 ? At(from, x + d, y) : At(from, x, y + d);
                    for (std::size_t c = 0; c < 4; c++) {
                        sum[c] += p[c] / static_cast<float>(radius * 2 + 1);
                    }
                }
                Put(out, x, y, sum);
            }
        }
    }
    return out;
}

} // namespace

GameIcon ComposeCover(const GameIcon& cover) {
    if (cover.w <= 0 || cover.h <= 0 ||
        cover.rgba.size() <
            static_cast<std::size_t>(cover.w) * static_cast<std::size_t>(cover.h) * 4) {
        return {};
    }
    // Behind: a small copy, softened, drawn large and dimmed towards the art area's colour
    constexpr int kSmall = 6;
    constexpr Pixel kGround{0x1D, 0x20, 0x24, 255.0f};
    constexpr float kDim = 0.5f;
    const GameIcon soft =
        Filled(Blurred(Filled(cover, kCoverPxW / kSmall, kCoverPxH / kSmall), 3), kCoverPxW, kCoverPxH);
    GameIcon out = Blank(kCoverPxW, kCoverPxH);
    // In front: the whole cover, in proportion
    const float scale =
        std::min(static_cast<float>(kCoverPxW) / static_cast<float>(cover.w),
                 static_cast<float>(kCoverPxH) / static_cast<float>(cover.h));
    const int w = std::clamp(static_cast<int>(std::lround(static_cast<float>(cover.w) * scale)), 1, kCoverPxW);
    const int h = std::clamp(static_cast<int>(std::lround(static_cast<float>(cover.h) * scale)), 1, kCoverPxH);
    const int x0 = (kCoverPxW - w) / 2, y0 = (kCoverPxH - h) / 2;
    const float sx = static_cast<float>(cover.w) / static_cast<float>(w);
    const float sy = static_cast<float>(cover.h) / static_cast<float>(h);
    for (int y = 0; y < kCoverPxH; y++) {
        for (int x = 0; x < kCoverPxW; x++) {
            Pixel p = At(soft, x, y);
            for (std::size_t c = 0; c < 3; c++) {
                p[c] = kGround[c] + (p[c] - kGround[c]) * kDim * p[3] / 255.0f;
            }
            p[3] = 255.0f;
            if (x >= x0 && x < x0 + w && y >= y0 && y < y0 + h) {
                const float u = static_cast<float>(x - x0), v = static_cast<float>(y - y0);
                const Pixel front = Mean(cover, u * sx, v * sy, (u + 1.0f) * sx, (v + 1.0f) * sy);
                for (std::size_t c = 0; c < 3; c++) {
                    p[c] += (front[c] - p[c]) * front[3] / 255.0f;
                }
            }
            Put(out, x, y, p);
        }
    }
    return out;
}

} // namespace FrameXr::Menu
