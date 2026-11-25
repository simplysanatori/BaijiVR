// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "VideoCommon/VR/FrameUI/toast_fit.h"
#include "VideoCommon/VR/FrameUI/ui_canvas.h"

// Azahar for Frame's hint (hud.cpp) under a screen that a grip holds, in a row of the toasts. The
// font has no arrows: they are drawn.
namespace VR::UI
{
inline FrameXr::ToastFit::Box DrawGrabHint(frame_xr::ui::Canvas* canvas, int row)
{
  namespace ToastFit = FrameXr::ToastFit;
  namespace ui = frame_xr::ui;
  constexpr float ARROW_W = 26.0f;
  enum class Arrow
  {
    None,
    UpDown,
    LeftRight,
  };
  struct Piece
  {
    const char* text;
    Arrow arrow = Arrow::None;
  };
  static constexpr std::array FIRST{Piece{"D-Pad "}, Piece{"", Arrow::UpDown}, Piece{" Curve   "},
                                    Piece{"", Arrow::LeftRight}, Piece{" Nudge"}};
  static constexpr const char* SECOND = "Stick: Size, Distance";

  const auto width_of = [](const Piece& piece) {
    return piece.arrow != Arrow::None ? ARROW_W : ui::TextWidth(ui::kFontSmall, piece.text);
  };
  float width = 0.0f;
  for (const Piece& piece : FIRST)
    width += width_of(piece);

  // The box of two lines.
  const float widest = std::max(width, ui::TextWidth(ui::kFontSmall, SECOND));
  ToastFit::Box box;
  box.two_lines = true;
  box.box_px = std::max(ToastFit::kMinBoxPx,
                        static_cast<int>(std::ceil(widest + 2.0f * ToastFit::kPadPx)));
  box.width_m = static_cast<float>(box.box_px) * ToastFit::kMetresPerPx;
  box.height_m = static_cast<float>(ToastFit::kRowPx) * ToastFit::kMetresPerPx;

  const auto row_pixels = static_cast<std::size_t>(ToastFit::kRowPx * canvas->w);
  std::fill_n(canvas->px.begin() + static_cast<std::ptrdiff_t>(row * row_pixels), row_pixels, 0u);
  const float top = static_cast<float>(row * ToastFit::kRowPx);
  const float right = static_cast<float>(box.box_px);
  canvas->RoundRect(2, top + 2, right - 2, top + ToastFit::kRowPx - 2, 36,
                    ui::rgb(28, 28, 34, 0.92f));
  const ui::Color white = ui::rgb(240, 240, 245);
  const float baseline = top + 40.0f;
  float x = right * 0.5f - width * 0.5f;
  for (const Piece& piece : FIRST)
  {
    if (piece.arrow == Arrow::None)
    {
      canvas->Text(x, baseline, ui::kFontSmall, piece.text, white);
    }
    else
    {
      // A stem and a head at each end.
      constexpr float HALF = 10.0f, STEM = 3.0f, HEAD = 6.0f;
      const bool up = piece.arrow == Arrow::UpDown;
      const float cx = x + ARROW_W * 0.5f;
      const float cy = baseline - 10.0f;
      const auto line = [&](float along0, float across0, float along1, float across1) {
        if (up)
          canvas->Line(cx + across0, cy + along0, cx + across1, cy + along1, STEM, white);
        else
          canvas->Line(cx + along0, cy + across0, cx + along1, cy + across1, STEM, white);
      };
      line(-HALF, 0.0f, HALF, 0.0f);
      for (const float end : {-HALF, HALF})
      {
        const float back = end < 0.0f ? end + HEAD : end - HEAD;
        line(end, 0.0f, back, -HEAD);
        line(end, 0.0f, back, HEAD);
      }
    }
    x += width_of(piece);
  }
  canvas->Text(right * 0.5f, top + 78.0f, ui::kFontSmall, SECOND, white, 1);
  return box;
}
}  // namespace VR::UI
