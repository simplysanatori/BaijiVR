// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/PadArt.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

namespace VR::UI
{
namespace
{
using namespace FrameXr::Menu;

constexpr float PI = std::numbers::pi_v<float>;

struct Point
{
  float x = 0.0f;
  float y = 0.0f;
};

constexpr Point operator+(Point a, Point b)
{
  return {a.x + b.x, a.y + b.y};
}

constexpr Point operator-(Point a, Point b)
{
  return {a.x - b.x, a.y - b.y};
}

constexpr Point operator*(Point a, float k)
{
  return {a.x * k, a.y * k};
}

float Length(Point a)
{
  return std::hypot(a.x, a.y);
}

Point Direction(float angle)
{
  return {std::cos(angle), std::sin(angle)};
}

using Contour = std::vector<Point>;
using Contours = std::vector<Contour>;

// Coverage of a shape, in canvas pixels.
struct Mask
{
  int x0 = 0;
  int y0 = 0;
  int w = 0;
  int h = 0;
  std::vector<float> a;

  float* Row(int y) { return a.data() + static_cast<size_t>(y) * static_cast<size_t>(w); }
  const float* Row(int y) const
  {
    return a.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
  }
  float At(int x, int y) const
  {
    x -= x0;
    y -= y0;
    return x >= 0 && x < w && y >= 0 && y < h ? Row(y)[x] : 0.0f;
  }
};

// Adds an edge to the mask's signed area differences (summed along the rows later).
void AccumulateEdge(Mask* mask, Point p0, Point p1)
{
  if (p0.y == p1.y)
    return;
  float dir = 1.0f;
  if (p0.y > p1.y)
  {
    std::swap(p0, p1);
    dir = -1.0f;
  }
  const float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
  float x = p0.x;
  if (p0.y < 0.0f)
    x -= p0.y * dxdy;
  const int first = std::max(0, static_cast<int>(p0.y));
  const int last = std::min(mask->h, static_cast<int>(std::ceil(p1.y)));
  const float limit = static_cast<float>(mask->w) - 2.0f;
  for (int y = first; y < last; ++y)
  {
    float* const row = mask->Row(y);
    const float fy = static_cast<float>(y);
    const float dy = std::min(fy + 1.0f, p1.y) - std::max(fy, p0.y);
    const float x_next = x + dxdy * dy;
    const float d = dy * dir;
    const float xa = std::clamp(std::min(x, x_next), 0.0f, limit);
    const float xb = std::clamp(std::max(x, x_next), 0.0f, limit);
    x = x_next;

    const float xa_floor = std::floor(xa);
    const float xb_ceil = std::ceil(xb);
    const int ia = static_cast<int>(xa_floor);
    const int ib = static_cast<int>(xb_ceil);
    if (ib <= ia + 1)
    {
      const float middle = 0.5f * (xa + xb) - xa_floor;
      row[ia] += d - d * middle;
      row[ia + 1] += d * middle;
      continue;
    }
    const float s = 1.0f / (xb - xa);
    const float fa = xa - xa_floor;
    const float a0 = 0.5f * s * (1.0f - fa) * (1.0f - fa);
    const float fb = xb - xb_ceil + 1.0f;
    const float am = 0.5f * s * fb * fb;
    row[ia] += d * a0;
    if (ib == ia + 2)
    {
      row[ia + 1] += d * (1.0f - a0 - am);
    }
    else
    {
      const float a1 = s * (1.5f - fa);
      row[ia + 1] += d * (a1 - a0);
      for (int i = ia + 2; i < ib - 1; ++i)
        row[i] += d * s;
      const float a2 = a1 + static_cast<float>(ib - ia - 3) * s;
      row[ib - 1] += d * (1.0f - a2 - am);
    }
    row[ib] += d * am;
  }
}

// Draws shapes given in a controller's own units.
class Painter
{
public:
  // `scale` is dp per unit; `origin` (dp) is where (0, 0) goes. `turned`: a quarter turn to the
  // left (what was up points left).
  Painter(Kit* kit, Canvas* canvas, Point origin, float scale, bool turned = false)
      : m_kit(kit), m_canvas(canvas), m_origin(origin), m_scale(scale), m_turned(turned)
  {
  }

  // The same size, but upright and around a point.
  Painter UprightAt(Point p) const { return Painter(m_kit, m_canvas, Dp(p), m_scale); }

  Point Dp(Point p) const
  {
    const Point turned = m_turned ? Point{p.y, -p.x} : p;
    return m_origin + turned * m_scale;
  }

  Point Px(Point p) const { return Dp(p) * m_kit->Px(); }
  float UnitPx() const { return m_scale * m_kit->Px(); }
  // A width in dp, in units.
  float FromDp(float dp) const { return dp / m_scale; }

  // An SVG-like path: absolute M, L, C. Mirrored, it is flipped across x = `mirror_x`.
  Contours Path(std::string_view svg, bool mirror = false, float mirror_x = 0.0f) const
  {
    Contours out;
    const char* s = svg.data();
    const char* const end = s + svg.size();
    char op = 0;
    Point at;
    const auto skip = [&] {
      while (s < end && *s == ' ')
        ++s;
    };
    const auto number = [&] {
      skip();
      const bool negative = s < end && *s == '-';
      if (negative)
        ++s;
      float value = 0.0f;
      float digit = 0.0f;  // below the point
      for (; s < end && ((*s >= '0' && *s <= '9') || *s == '.'); ++s)
      {
        if (*s == '.')
        {
          digit = 0.1f;
        }
        else if (digit > 0.0f)
        {
          value += static_cast<float>(*s - '0') * digit;
          digit *= 0.1f;
        }
        else
        {
          value = value * 10.0f + static_cast<float>(*s - '0');
        }
      }
      return negative ? -value : value;
    };
    const auto point = [&] {
      Point p;
      p.x = number();
      p.y = number();
      if (mirror)
        p.x = 2.0f * mirror_x - p.x;
      return p;
    };
    for (skip(); s < end; skip())
    {
      if (*s >= 'A' && *s <= 'Z')
        op = *s++;
      if (op == 'M')
      {
        at = point();
        out.push_back({Px(at)});
      }
      else if (op == 'L')
      {
        at = point();
        out.back().push_back(Px(at));
      }
      else if (op == 'C')
      {
        const Point c0 = point();
        const Point c1 = point();
        const Point to = point();
        Cubic(&out.back(), at, c0, c1, to);
        at = to;
      }
      else
      {
        break;
      }
    }
    return out;
  }

  Contours Line(Point a, Point b) const { return {{Px(a), Px(b)}}; }

  Contours Arc(Point c, float radius, float from, float to) const
  {
    const int steps = std::max(
        8, static_cast<int>(std::fabs(to - from) * std::sqrt(radius * UnitPx()) * 1.6f));
    Contour out;
    for (int i = 0; i <= steps; ++i)
    {
      const float t = from + (to - from) * static_cast<float>(i) / static_cast<float>(steps);
      out.push_back(Px(c + Direction(t) * radius));
    }
    return {out};
  }

  Contours Circle(Point c, float r) const { return Arc(c, r, 0.0f, 2.0f * PI); }

  Contours RoundRect(float x0, float y0, float x1, float y1, float r) const
  {
    return Rounded({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, r);
  }

  // An octagon with a flat top and rounded corners.
  Contours Octagon(Point c, float r, float rounding) const
  {
    std::vector<Point> corners;
    for (int i = 0; i < 8; ++i)
      corners.push_back(c + Direction(PI * (static_cast<float>(i) + 0.5f) / 4.0f) * r);
    return Rounded(corners, rounding);
  }

  // An octagon that stands on a corner.
  Contours Gate(Point c, float r, float rounding) const
  {
    std::vector<Point> corners;
    for (int i = 0; i < 8; ++i)
      corners.push_back(c + Direction(PI * static_cast<float>(i) / 4.0f) * r);
    return Rounded(corners, rounding);
  }

  // A polygon with rounded corners.
  Contours Rounded(const std::vector<Point>& corners, float radius) const
  {
    Contour out;
    const size_t n = corners.size();
    for (size_t i = 0; i < n; ++i)
    {
      const Point p = corners[i];
      const Point before = corners[(i + n - 1) % n] - p;
      const Point after = corners[(i + 1) % n] - p;
      const Point a = p + before * std::min(0.5f, radius / Length(before));
      const Point b = p + after * std::min(0.5f, radius / Length(after));
      out.push_back(Px(a));
      Cubic(&out, a, a + (p - a) * 0.56f, b + (p - b) * 0.56f, b);
    }
    return {out};
  }

  Contours Cross(Point c, float arm, float half_width, float rounding) const
  {
    const float a = arm;
    const float h = half_width;
    std::vector<Point> corners = {{-h, -a}, {h, -a}, {h, -h}, {a, -h}, {a, h},   {h, h},
                                  {h, a},   {-h, a}, {-h, h}, {-a, h}, {-a, -h}, {-h, -h}};
    for (Point& p : corners)
      p = p + c;
    return Rounded(corners, rounding);
  }

  // A thick arc with round ends.
  Contours Bean(Point c, float radius, float half_width, float from, float to) const
  {
    Contour out;
    const auto add = [&](Point centre, float r, float a0, float a1) {
      const Contour arc = Arc(centre, r, a0, a1)[0];
      out.insert(out.end(), arc.begin(), arc.end());
    };
    add(c, radius + half_width, from, to);
    add(c + Direction(to) * radius, half_width, to, to + PI);
    add(c, radius - half_width, to, from);
    add(c + Direction(from) * radius, half_width, from + PI, from + 2.0f * PI);
    return {out};
  }

  Mask Cover(const Contours& shape, float margin = 0.0f) const
  {
    float x0 = 1e9f;
    float y0 = 1e9f;
    float x1 = -1e9f;
    float y1 = -1e9f;
    for (const Contour& contour : shape)
    {
      for (const Point p : contour)
      {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
      }
    }
    // A little around the canvas is kept, for blurs.
    constexpr float KEEP = 64.0f;
    const float right = static_cast<float>(m_canvas->w) + KEEP;
    const float bottom = static_cast<float>(m_canvas->h) + KEEP;
    x0 = std::clamp(std::floor(x0 - margin), -KEEP, right);
    y0 = std::clamp(std::floor(y0 - margin), -KEEP, bottom);
    x1 = std::clamp(std::ceil(x1 + margin) + 2.0f, x0 + 3.0f, right + 3.0f);
    y1 = std::clamp(std::ceil(y1 + margin) + 1.0f, y0 + 1.0f, bottom + 1.0f);
    Mask mask;
    mask.x0 = static_cast<int>(x0);
    mask.y0 = static_cast<int>(y0);
    mask.w = static_cast<int>(x1 - x0);
    mask.h = static_cast<int>(y1 - y0);
    mask.a.assign(static_cast<size_t>(mask.w) * static_cast<size_t>(mask.h), 0.0f);
    const Point offset{x0, y0};
    for (const Contour& contour : shape)
    {
      for (size_t i = 0; i < contour.size(); ++i)
        AccumulateEdge(&mask, contour[i] - offset, contour[(i + 1) % contour.size()] - offset);
    }
    for (int y = 0; y < mask.h; ++y)
    {
      float* const row = mask.Row(y);
      float sum = 0.0f;
      for (int x = 0; x < mask.w; ++x)
      {
        sum += row[x];
        row[x] = std::min(std::fabs(sum), 1.0f);
      }
    }
    return mask;
  }

  void Fill(const Contours& shape, uint32_t rgb, float opacity = 1.0f) const
  {
    Paint(Cover(shape), rgb, opacity);
  }

  // Lines `width` units wide, round ended. For smooth lines: sharp corners are not mitred.
  void Stroke(const Contours& lines, float width, uint32_t rgb, float opacity = 1.0f,
              bool closed = false) const
  {
    const float h = width * UnitPx() * 0.5f;
    Contours shape;
    for (const Contour& given : lines)
    {
      Contour line;
      for (const Point p : given)
      {
        if (line.empty() || Length(p - line.back()) > 0.01f)
          line.push_back(p);
      }
      if (closed && line.size() > 1 && Length(line.front() - line.back()) <= 0.01f)
        line.pop_back();
      const size_t n = line.size();
      if (n < 2)
        continue;
      Contour one_side;
      Contour other_side;
      for (size_t i = 0; i < n; ++i)
      {
        const Point before = closed ? line[(i + n - 1) % n] : line[i ? i - 1 : 0];
        const Point after = closed ? line[(i + 1) % n] : line[std::min(i + 1, n - 1)];
        const Point along = after - before;
        const Point side = Point{-along.y, along.x} * (h / std::max(Length(along), 1e-3f));
        one_side.push_back(line[i] + side);
        other_side.push_back(line[i] - side);
      }
      std::ranges::reverse(other_side);
      if (closed)
      {
        shape.push_back(std::move(one_side));
        shape.push_back(std::move(other_side));
        continue;
      }
      const auto cap = [&](Point from, Point centre) {
        const Point d = from - centre;
        const float angle = std::atan2(d.y, d.x);
        for (int k = 1; k < 8; ++k)
          one_side.push_back(centre + Direction(angle - PI * static_cast<float>(k) / 8.0f) * h);
      };
      cap(one_side.back(), line.back());
      one_side.insert(one_side.end(), other_side.begin(), other_side.end());
      cap(one_side.back(), line.front());
      shape.push_back(std::move(one_side));
    }
    Fill(shape, rgb, opacity);
  }

  // A dashed closed line.
  void StrokeDashed(const Contour& line, float width, float dash, uint32_t rgb) const
  {
    const float period = dash * 2.0f * UnitPx();
    Contours pieces;
    float run = 0.0f;
    bool on = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
      const Point a = line[i];
      const Point step = line[(i + 1) % line.size()] - a;
      const float length = Length(step);
      const int count = std::max(1, static_cast<int>(length));
      for (int k = 0; k < count; ++k)
      {
        const float t = static_cast<float>(k) / static_cast<float>(count);
        const bool was_on = std::exchange(on, std::fmod(run + length * t, period) < period * 0.5f);
        if (on && !was_on)
          pieces.emplace_back();
        if (on)
          pieces.back().push_back(a + step * t);
      }
      run += length;
    }
    Stroke(pieces, width, rgb);
  }

  // Text centred on a point, upright, `size` units per em.
  void Letter(Point p, float size, uint32_t rgb, const std::string& text) const
  {
    const Point at = Dp(p);
    const float size_dp = size * m_scale;
    const float cap = m_kit->CapHeightSized(size_dp, true);
    m_kit->TextSized(at.x, at.y + cap * 0.5f, size_dp, true, rgb, text, 1);
  }

private:
  void Put(int x, int y, const Color& color, float coverage) const
  {
    if (x >= 0 && y >= 0 && x < m_canvas->w && y < m_canvas->h)
      m_canvas->Blend(x, y, color, coverage);
  }

  void Paint(const Mask& mask, uint32_t rgb, float opacity) const
  {
    const Color color = Rgb(rgb, opacity);
    for (int y = 0; y < mask.h; ++y)
    {
      const float* const row = mask.Row(y);
      for (int x = 0; x < mask.w; ++x)
      {
        if (row[x] > 0.002f)
          Put(mask.x0 + x, mask.y0 + y, color, row[x]);
      }
    }
  }

  void Cubic(Contour* out, Point p0, Point p1, Point p2, Point p3) const
  {
    const float length = Length(p1 - p0) + Length(p2 - p1) + Length(p3 - p2);
    const int steps = std::clamp(static_cast<int>(std::sqrt(length * UnitPx()) * 1.4f) + 2, 4, 48);
    for (int i = 1; i <= steps; ++i)
    {
      const float t = static_cast<float>(i) / static_cast<float>(steps);
      const float u = 1.0f - t;
      out->push_back(Px(p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) +
                        p3 * (t * t * t)));
    }
  }

  Kit* m_kit;
  Canvas* m_canvas;
  Point m_origin;
  float m_scale;
  bool m_turned;
};

// --- The drawings: outlines over a dark fill, as the controllers of Azahar for Frame's help ---

constexpr uint32_t LINE = 0xE6E6EA;
constexpr uint32_t BODY = 0x1F2125;
constexpr uint32_t PART = 0x323238;
constexpr float OUTLINE_DP = 1.7f;
constexpr float DETAIL_DP = 1.1f;

// A shape in front of what is drawn so far.
void Solid(const Painter& p, const Contours& shape, uint32_t fill, float line_dp = OUTLINE_DP)
{
  p.Fill(shape, fill);
  p.Stroke(shape, p.FromDp(line_dp), LINE, 1.0f, true);
}

void Detail(const Painter& p, const Contours& lines, bool closed = false)
{
  p.Stroke(lines, p.FromDp(DETAIL_DP), LINE, 0.75f, closed);
}

// The letter of a control that nothing presses: the others have their callout's dot there.
void Unbound(const Painter& p, const PadLabels& labels, PadControl control, Point at, float size,
             const char* letter)
{
  if (labels[control].empty())
    p.Letter(at, size, LINE, letter);
}

// --- The GameCube controller, in millimetres: the middle of its top edge at (0, 0) ---

constexpr Point GC_STICK{-42.0f, 33.0f};
constexpr Point GC_DPAD{-22.0f, 62.5f};
constexpr Point GC_CSTICK{22.0f, 62.5f};
constexpr Point GC_A{42.0f, 33.0f};
constexpr Point GC_B{26.5f, 40.0f};
constexpr Point GC_X{57.5f, 30.0f};
constexpr Point GC_Y{38.0f, 17.6f};
constexpr Point GC_START{0.0f, 33.0f};
constexpr Point GC_L{-45.0f, 1.2f};
constexpr Point GC_R{45.0f, -2.4f};
constexpr Point GC_Z{52.5f, 7.6f};

void DrawGameCube(const Painter& p, const PadLabels& labels)
{
  using enum PadControl;

  // The cable.
  Solid(p, p.Path("M -2.6 3 L -2 -9 L 2 -9 L 2.6 3"), BODY);
  for (const float y : {-6.5f, -4.0f, -1.5f})
    Detail(p, p.Line({-2.1f, y}, {2.1f, y}));

  // L and R, then Z, behind the body.
  for (const bool right : {false, true})
  {
    Solid(p, p.Path("M -65 22 C -65 6 -56 -6 -43 -7 C -33 -7.5 -26 -5 -24 1 L -24 12", right), PART);
  }
  Solid(p, p.Path("M 27 5 C 27 1 30 0 34 0.4 C 47 1.6 61 7.5 66 16.5 C 68 20.5 66 24 62 24"), PART);

  // The grips.
  for (const bool right : {false, true})
  {
    Solid(p,
          p.Path("M -67 36 C -72 54 -70.5 80 -65.5 94 C -62.5 102.5 -53.5 103.5 -49.5 95.5 "
                 "C -45 86 -43.5 77 -39 69 L -42 50",
                 right),
          BODY);
  }

  // The face.
  constexpr std::string_view HALF =
      "M 0 0 C -13 0 -25 1.4 -35 5.2 C -42 7.8 -46 9 -51 11.6 C -61 16.6 -68 24.5 -68 35.5 "
      "C -68 47.5 -60.5 56.5 -49.5 60.4 C -44 62.4 -42.4 63.6 -41 68 C -38.4 77 -31 83 -22 83 "
      "C -11.5 83 -3.2 74 -3.2 63.5 C -3.2 57.5 -5 54.4 -7.6 51.4 C -9.8 48.6 -8 47 -4 47 L 0 47";
  Contour face = p.Path(HALF)[0];
  const Contour mirrored = p.Path(HALF, true)[0];
  face.insert(face.end(), mirrored.rbegin(), mirrored.rend());
  Solid(p, {face}, BODY);

  // The hollow the buttons are in.
  Detail(p, p.Arc({43.5f, 31.5f}, 22.0f, -PI * 1.10f, PI * 0.46f));

  // The control stick.
  Detail(p, p.Circle(GC_STICK, 19.5f), true);
  Solid(p, p.Octagon(GC_STICK, 14.6f, 2.0f), BODY);
  Solid(p, p.Circle(GC_STICK, 10.2f), PART);
  Detail(p, p.Circle(GC_STICK, 7.2f), true);
  Detail(p, p.Circle(GC_STICK, 4.2f), true);

  // The D-pad.
  Detail(p, p.Circle(GC_DPAD, 13.0f), true);
  Solid(p, p.Cross(GC_DPAD, 10.0f, 3.5f, 1.2f), PART);

  // The C stick.
  Solid(p, p.Octagon(GC_CSTICK, 13.6f, 2.0f), BODY);
  Solid(p, p.Circle(GC_CSTICK, 7.8f), PART);
  Detail(p, p.Circle(GC_CSTICK, 5.4f), true);
  Unbound(p, labels, CStick, GC_CSTICK, 7.0f, "C");

  // The buttons.
  Solid(p, p.Bean(GC_A, 15.9f, 4.3f, -PI * 0.71f, -PI * 0.455f), PART);
  Solid(p, p.Bean(GC_A, 15.9f, 4.3f, -PI * 0.185f, PI * 0.07f), PART);
  Solid(p, p.Circle(GC_A, 9.2f), PART);
  Solid(p, p.Circle(GC_B, 5.6f), PART);
  Solid(p, p.Circle(GC_START, 3.6f), PART);
  Unbound(p, labels, A, GC_A, 10.0f, "A");
  Unbound(p, labels, B, GC_B, 6.5f, "B");
  Unbound(p, labels, X, GC_X, 6.0f, "X");
  Unbound(p, labels, Y, GC_Y, 6.0f, "Y");
  Unbound(p, labels, L, GC_L, 6.5f, "L");
  Unbound(p, labels, R, GC_R, 6.5f, "R");
  Unbound(p, labels, Z, GC_Z, 5.0f, "Z");
}

// --- The Wii Remote, 750 units for its 148 mm: the middle of its top edge at (0, 0) ---

constexpr float REMOTE_HALF_W = 92.0f;
constexpr float REMOTE_H = 750.0f;
constexpr float REMOTE_FRONT = 12.0f;  // the pointer's window, past the top edge
constexpr Point WII_POINTER{0.0f, -6.0f};
constexpr Point WII_POWER{-54.0f, 34.0f};
constexpr Point WII_DPAD{0.0f, 102.0f};
constexpr Point WII_A{0.0f, 213.0f};
constexpr Point WII_B{46.0f, 182.0f};
constexpr Point WII_MINUS{-55.0f, 337.0f};
constexpr Point WII_HOME{0.0f, 337.0f};
constexpr Point WII_PLUS{55.0f, 337.0f};
constexpr Point WII_ONE{0.0f, 531.0f};
constexpr Point WII_TWO{0.0f, 598.0f};
constexpr Point WII_SHAKE{60.0f, 712.0f};

void DrawRemote(const Painter& p, const PadLabels& labels)
{
  using enum PadControl;

  // The pointer's window, on the front end.
  Solid(p, p.RoundRect(-62.0f, -REMOTE_FRONT, 62.0f, 20.0f, 9.0f), PART);
  Solid(p, p.RoundRect(-REMOTE_HALF_W, 0.0f, REMOTE_HALF_W, REMOTE_H, 26.0f), BODY);

  // Power.
  Detail(p, p.Circle(WII_POWER, 16.0f), true);

  // B, on the back.
  p.StrokeDashed(p.Rounded({{-54.0f, 160.0f},
                            {54.0f, 160.0f},
                            {62.0f, 176.0f},
                            {52.0f, 284.0f},
                            {-52.0f, 284.0f},
                            {-62.0f, 176.0f}},
                           16.0f)[0],
                 p.FromDp(DETAIL_DP), 7.0f, 0x92939A);

  Solid(p, p.Cross(WII_DPAD, 50.0f, 17.0f, 5.0f), PART);
  Solid(p, p.Circle(WII_A, 33.0f), PART);
  for (const Point c : {WII_MINUS, WII_HOME, WII_PLUS})
    Solid(p, p.Circle(c, 18.0f), PART);
  Unbound(p, labels, A, WII_A, 34.0f, "A");
  Unbound(p, labels, B, {0.0f, 264.0f}, 24.0f, "B");
  Unbound(p, labels, Minus, WII_MINUS, 26.0f, "-");
  Unbound(p, labels, Plus, WII_PLUS, 26.0f, "+");

  // The speaker.
  for (int row = 0; row < 6; ++row)
  {
    for (int column = 0; column < 4; ++column)
    {
      p.Fill(p.Circle({-18.0f + 12.0f * static_cast<float>(column),
                       400.0f + 12.0f * static_cast<float>(row)},
                      2.6f),
             LINE, 0.6f);
    }
  }

  Solid(p, p.Circle(WII_ONE, 24.0f), PART);
  Solid(p, p.Circle(WII_TWO, 24.0f), PART);
  Unbound(p, labels, One, WII_ONE, 28.0f, "1");
  Unbound(p, labels, Two, WII_TWO, 28.0f, "2");

  // The player lights.
  for (int i = 0; i < 4; ++i)
  {
    const float x = -54.0f + 36.0f * static_cast<float>(i);
    Detail(p, p.RoundRect(x - 5.5f, 650.0f, x + 5.5f, 661.0f, 1.5f), true);
  }
}

// --- The Nunchuk, from its left side, in the remote's units ---

constexpr Point NUNCHUK_STICK{191.0f, 23.0f};
constexpr Point NUNCHUK_C{15.0f, 92.0f};
constexpr Point NUNCHUK_Z{16.0f, 166.0f};

void DrawNunchuk(const Painter& p, const PadLabels& labels)
{
  using enum PadControl;

  // C and Z, then the stick, behind the shell.
  Solid(p, p.Path("M 58 60 C 24 56 4 70 1 86 C -2 102 8 116 26 122 L 58 124"), PART);
  Solid(p,
        p.Path("M 60 140 L 8 130 C 0 129 -4 133 -3 140 L 5 192 C 6 199 10 202 16 203 L 60 210"),
        PART);
  Solid(p, p.Path("M 211 34 L 197 74 L 208 110 L 150 90 L 157 58 L 167 18"), BODY);
  Solid(p,
        p.Path("M 232 37 C 235 27 219 18 195 11 C 171 3 153 0 150 10 C 147 20 163 30 186 38 "
               "C 210 46 229 47 232 37"),
        PART);

  Solid(p, p.Path("M 372 480 L 378 532 L 400 530 L 400 478"), PART);
  Solid(p,
        p.Path("M 32 59 C 35 44 42 30 53 26 C 75 19 105 29 127 46 L 227 87 C 250 102 268 122 279 138 "
               "C 340 228 384 373 401 451 C 408 478 401 492 386 491 C 337 529 274 517 226 455 "
               "C 176 390 168 293 131 251 C 109 225 83 219 64 221 C 46 223 39 207 37 188 L 27 106 "
               "C 25 89 28 76 32 59"),
        BODY);
  // The shell's seam, and the stick's collar.
  Detail(p, p.Path("M 32 59 C 107 107 207 205 274 307 C 322 378 357 455 386 491"));
  Detail(p, p.Path("M 127 46 C 148 64 190 84 227 87"));
  Unbound(p, labels, C, NUNCHUK_C, 24.0f, "C");
  Unbound(p, labels, NunchukZ, NUNCHUK_Z, 24.0f, "Z");
}

// The Nunchuk and the remote together: where each's own (0, 0) is.
constexpr Point NUNCHUK_AT{0.0f, 130.0f};
constexpr Point REMOTE_AT{528.0f, 0.0f};

void DrawRemoteAndNunchuk(const Painter& p, const PadLabels& labels)
{
  // The cord.
  const Contours cord = p.Path("M 389 655 C 394 740 392 836 456 838 C 516 840 528 826 528 776");
  p.Stroke(cord, p.FromDp(OUTLINE_DP * 2.0f) + 9.0f, LINE);
  p.Stroke(cord, 9.0f, BODY);
  Solid(p, p.RoundRect(494.0f, 730.0f, 562.0f, 786.0f, 10.0f), PART);

  // The scale is the painter's, the place the part's.
  DrawNunchuk(p.UprightAt(NUNCHUK_AT), labels);
  DrawRemote(p.UprightAt(REMOTE_AT), labels);
}

// --- The Classic Controller, 1590 units wide: the middle of its width at 0 ---

constexpr Point CLASSIC_L{-505.0f, 46.0f};
constexpr Point CLASSIC_ZL{-195.0f, 24.0f};
constexpr Point CLASSIC_R{505.0f, 46.0f};
constexpr Point CLASSIC_ZR{195.0f, 24.0f};
constexpr Point CLASSIC_DPAD{-486.0f, 363.0f};
constexpr Point CLASSIC_MINUS{-118.0f, 367.0f};
constexpr Point CLASSIC_HOME{0.0f, 367.0f};
constexpr Point CLASSIC_PLUS{118.0f, 367.0f};
constexpr Point CLASSIC_X{495.0f, 262.0f};
constexpr Point CLASSIC_Y{337.0f, 368.0f};
constexpr Point CLASSIC_A{643.0f, 368.0f};
constexpr Point CLASSIC_B{483.0f, 471.0f};
constexpr Point CLASSIC_LEFT_STICK{-211.0f, 600.0f};
constexpr Point CLASSIC_RIGHT_STICK{211.0f, 600.0f};

void DrawClassic(const Painter& p, const PadLabels& labels)
{
  using enum PadControl;
  const auto whole = [&p](std::string_view half) {
    Contour shape = p.Path(half)[0];
    const Contour mirrored = p.Path(half, true)[0];
    shape.insert(shape.end(), mirrored.rbegin(), mirrored.rend());
    return Contours{std::move(shape)};
  };

  // The cable.
  Solid(p, p.Path("M -20 740 L -20 866 L 20 866 L 20 740"), BODY);

  // L and R, ZL and ZR, then the clip, behind the body.
  for (const bool right : {false, true})
  {
    Solid(p,
          p.Path("M -636 130 L -638 92 C -637 84 -632 79 -624 74 C -560 38 -470 12 -372 4 "
                 "C -362 3 -357 8 -357 16 L -356 120",
                 right),
          PART);
    Detail(p, p.Path("M -634 100 C -560 58 -465 32 -358 24", right));
    Solid(p,
          p.Path("M -250 70 L -246 30 C -243 16 -226 10 -205 9 L -168 9 C -150 10 -143 18 -141 30 "
                 "L -138 70",
                 right),
          PART);
  }
  Solid(p, p.RoundRect(-26.0f, 8.0f, 26.0f, 90.0f, 16.0f), PART);

  // The shell behind the face.
  Solid(p, whole("M 0 54 C -110 54 -180 42 -270 42 C -420 42 -560 70 -650 128 L -600 300 L 0 300"),
        BODY);

  // The face.
  Solid(p,
        whole("M 0 108 C -110 108 -190 82 -310 82 C -480 82 -600 104 -690 186 "
              "C -760 256 -795 330 -795 420 C -795 532 -732 622 -640 684 "
              "C -560 738 -460 768 -370 768 C -240 768 -140 752 0 752"),
        BODY);

  // The D-Pad.
  Solid(p, p.Cross(CLASSIC_DPAD, 140.0f, 47.0f, 12.0f), PART);
  for (const Point d : {Point{0.0f, -1.0f}, Point{0.0f, 1.0f}, Point{-1.0f, 0.0f}, Point{1.0f, 0.0f}})
    Detail(p, p.Line(CLASSIC_DPAD + d * 58.0f, CLASSIC_DPAD + d * 102.0f));

  for (const Point c : {CLASSIC_MINUS, CLASSIC_HOME, CLASSIC_PLUS})
    Solid(p, p.Circle(c, 35.0f), PART);
  Unbound(p, labels, Minus, CLASSIC_MINUS, 52.0f, "-");
  Unbound(p, labels, Plus, CLASSIC_PLUS, 52.0f, "+");

  // X and Y stand out, A and B are hollow.
  for (const Point c : {CLASSIC_X, CLASSIC_Y, CLASSIC_A, CLASSIC_B})
    Solid(p, p.Circle(c, 60.0f), PART);
  for (const Point c : {CLASSIC_A, CLASSIC_B})
    Detail(p, p.Circle(c, 47.0f), true);

  // The sticks.
  for (const Point c : {CLASSIC_LEFT_STICK, CLASSIC_RIGHT_STICK})
  {
    Solid(p, p.Gate(c, 124.0f, 10.0f), BODY);
    Solid(p, p.Circle(c, 92.0f), PART);
    Detail(p, p.Circle(c, 66.0f), true);
  }

  Unbound(p, labels, A, CLASSIC_A, 62.0f, "A");
  Unbound(p, labels, B, CLASSIC_B, 62.0f, "B");
  Unbound(p, labels, X, CLASSIC_X, 62.0f, "X");
  Unbound(p, labels, Y, CLASSIC_Y, 62.0f, "Y");
  Unbound(p, labels, L, CLASSIC_L + Point{0.0f, 14.0f}, 50.0f, "L");
  Unbound(p, labels, R, CLASSIC_R + Point{0.0f, 14.0f}, 50.0f, "R");
}

// --- Callouts ---

enum class Side
{
  Left,
  Right,
  Under,
};

// Where a control's callout starts. `bend`: the line goes there first, around what is beside it.
struct Anchor
{
  PadControl control;
  Side side;
  Point at;
  bool has_bend = false;
  Point bend{};
};

struct Callout
{
  Anchor anchor;
  std::string key;     // what presses it
  std::string action;  // the control
  std::vector<Point> line;  // from the control to the text
  float width = 0.0f;
  float want = 0.0f;  // where its text would like to be
  float row = 0.0f;   // the middle of its text
  float x0 = 0.0f;    // where its text starts
};

constexpr Type TEXT = Type::RailLabel;
constexpr float LABEL_GAP = 8.0f;  // between a line and its text
constexpr float GUTTER = 24.0f;    // between the texts and the controller
constexpr float MAX_GUTTER = 72.0f;
constexpr float MAX_LABEL_W = 220.0f;
constexpr float ROW_H = 26.0f;
constexpr float LEADER_DP = 1.5f;
constexpr float DOT_DP = 3.5f;

const char* ActionOf(PadControl control, PadKind kind)
{
  // By controller: the GameCube Controller's, the Wii Remote's, the Classic Controller's.
  const auto of = [kind](const char* gamecube, const char* remote, const char* classic) {
    return kind == PadKind::GameCube ? gamecube : kind == PadKind::WiiClassic ? classic : remote;
  };
  switch (control)
  {
  case PadControl::A:
    return of("GameCube A", "Wii A", "Classic A");
  case PadControl::B:
    return of("GameCube B", "Wii B", "Classic B");
  case PadControl::X:
    return of("GameCube X", "", "Classic X");
  case PadControl::Y:
    return of("GameCube Y", "", "Classic Y");
  case PadControl::Z:
    return "GameCube Z";
  case PadControl::L:
    return of("GameCube L", "", "Classic L");
  case PadControl::R:
    return of("GameCube R", "", "Classic R");
  case PadControl::Start:
    return "Start";
  case PadControl::Stick:
    return "Control Stick";
  case PadControl::CStick:
    return "C Stick";
  case PadControl::DPad:
    return of("GameCube D-Pad", "Wii D-Pad", "Classic D-Pad");
  case PadControl::One:
    return "Wii 1";
  case PadControl::Two:
    return "Wii 2";
  case PadControl::Plus:
    return of("", "Wii Plus", "Classic Plus");
  case PadControl::Minus:
    return of("", "Wii Minus", "Classic Minus");
  case PadControl::Home:
    return of("", "Wii Home", "Classic Home");
  case PadControl::Pointer:
    return "Pointer";
  case PadControl::Shake:
    return "Shake";
  case PadControl::C:
    return "Nunchuk C";
  case PadControl::NunchukZ:
    return "Nunchuk Z";
  case PadControl::NunchukStick:
    return "Nunchuk Stick";
  case PadControl::ZL:
    return "Classic ZL";
  case PadControl::ZR:
    return "Classic ZR";
  case PadControl::LeftStick:
    return "Classic Left Stick";
  case PadControl::RightStick:
    return "Classic Right Stick";
  case PadControl::Count:
    break;
  }
  return "";
}

float Turn(Point a, Point b, Point c)
{
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool LinesCross(const std::vector<Point>& a, const std::vector<Point>& b)
{
  for (size_t i = 0; i + 1 < a.size(); ++i)
  {
    for (size_t k = 0; k + 1 < b.size(); ++k)
    {
      if (Turn(a[i], a[i + 1], b[k]) * Turn(a[i], a[i + 1], b[k + 1]) < 0.0f &&
          Turn(b[k], b[k + 1], a[i]) * Turn(b[k], b[k + 1], a[i + 1]) < 0.0f)
      {
        return true;
      }
    }
  }
  return false;
}

// Shortens what presses it, not the control's name.
void FitTo(const Kit& kit, Callout* callout, float width)
{
  const float action = kit.TextWidth(TEXT, ": " + callout->action);
  callout->key = kit.Fit(TEXT, callout->key, std::max(0.0f, width - action));
  callout->width = kit.TextWidth(TEXT, callout->key + ": ") + kit.TextWidth(TEXT, callout->action);
}

std::vector<Callout> MakeCallouts(const Kit& kit, const std::vector<Anchor>& anchors, Side side,
                                  const PadLabels& labels, PadKind kind)
{
  std::vector<Callout> out;
  for (const Anchor& anchor : anchors)
  {
    const std::string& label = labels[anchor.control];
    if (anchor.side != side || label.empty())
      continue;
    Callout callout;
    callout.anchor = anchor;
    callout.key = label;
    callout.action = ActionOf(anchor.control, kind);
    FitTo(kit, &callout, MAX_LABEL_W);
    out.push_back(std::move(callout));
  }
  return out;
}

float WidthOf(const std::vector<Callout>& callouts)
{
  float width = 0.0f;
  for (const Callout& callout : callouts)
    width = std::max(width, callout.width);
  return width;
}

// The places nearest to `want` (in order) that are `gap[i]` apart from the one before and stay
// between `low` and `high`.
std::vector<float> Spread(const std::vector<float>& want, const std::vector<float>& gap, float low,
                          float high)
{
  // Groups that touch each other sit where they are off the least, together.
  struct Group
  {
    float sum;
    size_t count;
    float At() const { return sum / static_cast<float>(count); }
  };
  std::vector<Group> groups;
  std::vector<float> offset(want.size(), 0.0f);
  for (size_t i = 0; i < want.size(); ++i)
  {
    offset[i] = i == 0 ? 0.0f : offset[i - 1] + gap[i];
    groups.push_back({want[i] - offset[i], 1});
    while (groups.size() > 1 && groups[groups.size() - 2].At() > groups.back().At())
    {
      const Group last = groups.back();
      groups.pop_back();
      groups.back().sum += last.sum;
      groups.back().count += last.count;
    }
  }
  std::vector<float> out;
  for (const Group& group : groups)
  {
    for (size_t i = 0; i < group.count; ++i)
    {
      const float at = group.At();
      const float most = high - offset.back();
      out.push_back(std::max(low, std::min(at, most)) + offset[out.size()]);
    }
  }
  return out;
}

// Puts the callouts of one side down a column whose text starts (or ends) at `edge`, each as near
// its control's height as there is room.
void PlaceColumn(std::vector<Callout>* callouts, const Painter& painter, const Rect& area,
                 Side side, float edge)
{
  const size_t n = callouts->size();
  if (n == 0)
    return;
  const float pitch = std::min(ROW_H, area.H() / static_cast<float>(n));
  const auto place = [&] {
    std::vector<float> want;
    for (const Callout& callout : *callouts)
    {
      const Anchor& anchor = callout.anchor;
      want.push_back(painter.Dp(anchor.has_bend ? anchor.bend : anchor.at).y);
    }
    const std::vector<float> rows = Spread(want, std::vector<float>(n, pitch),
                                           area.y0 + pitch * 0.5f, area.y1 - pitch * 0.5f);
    for (size_t i = 0; i < n; ++i)
    {
      Callout& callout = (*callouts)[i];
      callout.row = rows[i];
      callout.x0 = side == Side::Left ? edge - callout.width : edge;
      callout.line = {painter.Dp(callout.anchor.at)};
      if (callout.anchor.has_bend)
        callout.line.push_back(painter.Dp(callout.anchor.bend));
      callout.line.push_back({edge + (side == Side::Left ? LABEL_GAP : -LABEL_GAP), rows[i]});
    }
  };

  // By height, then exchanged until no two lines cross.
  std::ranges::stable_sort(*callouts, [&](const Callout& a, const Callout& b) {
    return painter.Dp(a.anchor.has_bend ? a.anchor.bend : a.anchor.at).y <
           painter.Dp(b.anchor.has_bend ? b.anchor.bend : b.anchor.at).y;
  });
  place();
  bool changed = true;
  for (size_t pass = 0; changed && pass < n * n; ++pass)
  {
    changed = false;
    for (size_t i = 0; i + 1 < n && !changed; ++i)
    {
      if (LinesCross((*callouts)[i].line, (*callouts)[i + 1].line))
      {
        std::swap((*callouts)[i], (*callouts)[i + 1]);
        place();
        if (LinesCross((*callouts)[i].line, (*callouts)[i + 1].line))
        {
          std::swap((*callouts)[i], (*callouts)[i + 1]);
          place();
        }
        else
        {
          changed = true;
        }
      }
    }
  }
}

void DrawCallouts(Kit* kit, const std::vector<Callout>& callouts)
{
  for (const Callout& callout : callouts)
  {
    for (size_t i = 0; i + 1 < callout.line.size(); ++i)
    {
      const Point a = callout.line[i];
      const Point b = callout.line[i + 1];
      kit->Line(a.x, a.y, b.x, b.y, LEADER_DP, Tok::kOutline);
    }
  }
  for (const Callout& callout : callouts)
  {
    kit->Dot(callout.line.front().x, callout.line.front().y, DOT_DP, Tok::kPrimary);
    const std::string key = callout.key + ": ";
    kit->TextMid(callout.x0, callout.row, TEXT, Tok::kOnSurfaceVariant, key);
    kit->TextMid(callout.x0 + kit->TextWidth(TEXT, key), callout.row, TEXT, Tok::kOnSurface,
                 callout.action);
  }
}

struct Plan
{
  Rect bounds;  // of the drawing, in its units
  void (*draw)(const Painter&, const PadLabels&);
  PadKind kind;
  std::vector<Anchor> anchors;
};

Plan GameCubePlan()
{
  using enum PadControl;
  return {{-72.0f, -9.0f, 72.0f, 102.0f},
          DrawGameCube,
          PadKind::GameCube,
          {
              {L, Side::Left, GC_L},
              {Stick, Side::Left, GC_STICK},
              {DPad, Side::Left, GC_DPAD},
              {Start, Side::Under, GC_START},
              {R, Side::Right, GC_R},
              {Z, Side::Right, GC_Z},
              {Y, Side::Right, GC_Y},
              {X, Side::Right, GC_X},
              {A, Side::Right, GC_A},
              {B, Side::Right, GC_B},
              {CStick, Side::Right, GC_CSTICK},
          }};
}

// Over the buttons beside them, and under.
constexpr Point HOME_BEND{32.0f, 303.0f};
constexpr Point MINUS_BEND{-26.0f, 372.0f};

Plan RemotePlan()
{
  using enum PadControl;
  return {{-REMOTE_HALF_W, -REMOTE_FRONT - 4.0f, REMOTE_HALF_W, REMOTE_H + 4.0f},
          DrawRemote,
          PadKind::WiiRemote,
          {
              {Pointer, Side::Left, WII_POINTER},
              {DPad, Side::Left, WII_DPAD},
              {A, Side::Left, WII_A},
              {Minus, Side::Left, WII_MINUS},
              {One, Side::Left, WII_ONE},
              {B, Side::Right, WII_B},
              {Home, Side::Right, WII_HOME, true, HOME_BEND},
              {Plus, Side::Right, WII_PLUS},
              {Two, Side::Right, WII_TWO},
              {Shake, Side::Right, WII_SHAKE},
          }};
}

Plan NunchukPlan()
{
  using enum PadControl;
  return {{0.0f, -REMOTE_FRONT - 4.0f, REMOTE_AT.x + REMOTE_HALF_W, 850.0f},
          DrawRemoteAndNunchuk,
          PadKind::WiiNunchuk,
          {
              {NunchukStick, Side::Left, NUNCHUK_STICK + NUNCHUK_AT},
              {C, Side::Left, NUNCHUK_C + NUNCHUK_AT},
              {NunchukZ, Side::Left, NUNCHUK_Z + NUNCHUK_AT},
              {Pointer, Side::Right, WII_POINTER + REMOTE_AT},
              {DPad, Side::Right, WII_DPAD + REMOTE_AT},
              {B, Side::Right, WII_B + REMOTE_AT},
              {A, Side::Right, WII_A + REMOTE_AT},
              {Home, Side::Right, WII_HOME + REMOTE_AT, true, HOME_BEND + REMOTE_AT},
              {Plus, Side::Right, WII_PLUS + REMOTE_AT},
              {Minus, Side::Right, WII_MINUS + REMOTE_AT, true, MINUS_BEND + REMOTE_AT},
              {One, Side::Right, WII_ONE + REMOTE_AT},
              {Two, Side::Right, WII_TWO + REMOTE_AT},
              {Shake, Side::Right, WII_SHAKE + REMOTE_AT},
          }};
}

Plan ClassicPlan()
{
  using enum PadControl;
  return {{-800.0f, -4.0f, 800.0f, 866.0f},
          DrawClassic,
          PadKind::WiiClassic,
          {
              {ZL, Side::Left, CLASSIC_ZL, true, {-260.0f, -20.0f}},
              {L, Side::Left, CLASSIC_L},
              {Home, Side::Left, CLASSIC_HOME, true, {-70.0f, 150.0f}},
              {Minus, Side::Left, CLASSIC_MINUS, true, {-300.0f, 205.0f}},
              {DPad, Side::Left, CLASSIC_DPAD},
              {LeftStick, Side::Left, CLASSIC_LEFT_STICK},
              {ZR, Side::Right, CLASSIC_ZR, true, {260.0f, -20.0f}},
              {R, Side::Right, CLASSIC_R},
              {Plus, Side::Right, CLASSIC_PLUS, true, {300.0f, 180.0f}},
              {X, Side::Right, CLASSIC_X},
              {Y, Side::Right, CLASSIC_Y},
              {A, Side::Right, CLASSIC_A},
              {B, Side::Right, CLASSIC_B},
              {RightStick, Side::Right, CLASSIC_RIGHT_STICK},
          }};
}

Plan GetPlan(PadKind kind)
{
  switch (kind)
  {
  case PadKind::GameCube:
    return GameCubePlan();
  case PadKind::WiiNunchuk:
    return NunchukPlan();
  case PadKind::WiiClassic:
    return ClassicPlan();
  case PadKind::WiiRemote:
  case PadKind::WiiRemoteSideways:
    break;
  }
  return RemotePlan();
}

void DrawUpright(Kit* kit, Canvas* canvas, const Rect& area, const Plan& plan,
                 const PadLabels& labels)
{
  std::vector<Callout> left = MakeCallouts(*kit, plan.anchors, Side::Left, labels, plan.kind);
  std::vector<Callout> right = MakeCallouts(*kit, plan.anchors, Side::Right, labels, plan.kind);
  const float left_w = WidthOf(left);
  const float right_w = WidthOf(right);
  const float room = area.W() - left_w - right_w;

  const float w = plan.bounds.W();
  const float scale =
      std::max(0.05f, std::min((room - 2.0f * GUTTER) / w, area.H() / plan.bounds.H()));
  const float gutter = std::clamp((room - w * scale) * 0.5f, 0.0f, MAX_GUTTER);
  const float left_edge = area.Cx() + (left_w - right_w - w * scale) * 0.5f - gutter;
  const float right_edge = left_edge + 2.0f * gutter + w * scale;
  const Point origin{left_edge + gutter - plan.bounds.x0 * scale,
                     area.Cy() - plan.bounds.Cy() * scale};

  const Painter painter(kit, canvas, origin, scale);
  PlaceColumn(&left, painter, area, Side::Left, left_edge);
  PlaceColumn(&right, painter, area, Side::Right, right_edge);
  plan.draw(painter, labels);
  DrawCallouts(kit, left);
  DrawCallouts(kit, right);

  // Straight down, between the grips.
  std::vector<Callout> under = MakeCallouts(*kit, plan.anchors, Side::Under, labels, plan.kind);
  for (Callout& callout : under)
  {
    const Point at = painter.Dp(callout.anchor.at);
    FitTo(*kit, &callout, (plan.bounds.W() - 60.0f) * scale);
    callout.row = painter.Dp({0.0f, plan.bounds.y1}).y - ROW_H * 0.5f;
    callout.x0 = at.x - callout.width * 0.5f;
    callout.line = {at, {at.x, callout.row - ROW_H * 0.5f}};
  }
  DrawCallouts(kit, under);
}

// In the upright remote's units; the turn is the painter's. Left is above, right is below.
std::vector<Anchor> SidewaysAnchors()
{
  using enum PadControl;
  return {
      {DPad, Side::Left, WII_DPAD}, {A, Side::Left, WII_A},
      {Plus, Side::Left, WII_PLUS}, {One, Side::Left, WII_ONE},
      {B, Side::Right, {-WII_B.x, WII_B.y}},
      {Minus, Side::Right, WII_MINUS},
      {Home, Side::Right, WII_HOME, true, {-40.0f, 376.0f}},
      {Two, Side::Right, WII_TWO},  {Shake, Side::Right, {-WII_SHAKE.x, WII_SHAKE.y}},
  };
}

// The middle of an arm, from the D-Pad's.
float GetArm(PadKind kind)
{
  return kind == PadKind::GameCube ? 6.5f : kind == PadKind::WiiClassic ? 93.0f : 33.0f;
}

void DrawLinks(Kit* kit, const Painter& painter, PadKind kind, const std::vector<Anchor>& anchors,
               const std::vector<PadLink>& links)
{
  const float arm = GetArm(kind) / painter.FromDp(1.0f);
  for (const PadLink& link : links)
  {
    const auto anchor = std::ranges::find(anchors, link.control, &Anchor::control);
    if (anchor == anchors.end())
      continue;
    const Point at = painter.Dp(anchor->at) + Point{link.arm_x, link.arm_y} * arm;
    Point from = at;
    if (anchor->has_bend)
    {
      const Point bend = painter.Dp(anchor->bend);
      kit->Line(from.x, from.y, bend.x, bend.y, LEADER_DP, Tok::kPrimary);
      from = bend;
    }
    kit->Line(from.x, from.y, link.x, link.y, LEADER_DP, Tok::kPrimary);
    kit->Dot(at.x, at.y, DOT_DP, Tok::kPrimary);
  }
}

// The remote held sideways: its callouts are in a row above it and a row below.
void DrawSideways(Kit* kit, Canvas* canvas, const Rect& area, const PadLabels& labels)
{
  const std::vector<Anchor> anchors = SidewaysAnchors();
  constexpr float SPACE = 64.0f;  // between the remote and the texts
  constexpr float APART = 18.0f;  // between texts

  const float scale =
      std::max(0.05f, std::min(area.W() * 0.86f / (REMOTE_H + REMOTE_FRONT),
                               (area.H() - 2.0f * (SPACE + ROW_H)) / (2.0f * REMOTE_HALF_W)));
  const Point origin{area.Cx() - (REMOTE_H - REMOTE_FRONT) * scale * 0.5f, area.Cy()};
  const Painter remote(kit, canvas, origin, scale, true);
  DrawRemote(remote, labels);

  for (const Side side : {Side::Left, Side::Right})
  {
    std::vector<Callout> callouts = MakeCallouts(*kit, anchors, side, labels, PadKind::WiiRemoteSideways);
    const size_t n = callouts.size();
    if (n == 0)
      continue;
    std::ranges::stable_sort(callouts, [&](const Callout& a, const Callout& b) {
      return remote.Dp(a.anchor.at).x < remote.Dp(b.anchor.at).x;
    });

    // The long ones are cut short where they do not all fit.
    const float most = area.W() - APART * static_cast<float>(n - 1);
    for (float cap = MAX_LABEL_W; cap > 40.0f; cap -= 4.0f)
    {
      float total = 0.0f;
      for (Callout& callout : callouts)
      {
        if (callout.width > cap)
          FitTo(*kit, &callout, cap);
        total += callout.width;
      }
      if (total <= most)
        break;
    }

    std::vector<float> want;
    std::vector<float> gap;
    for (size_t i = 0; i < n; ++i)
    {
      const float half = callouts[i].width * 0.5f;
      want.push_back(remote.Dp(callouts[i].anchor.at).x);
      gap.push_back(i == 0 ? 0.0f : callouts[i - 1].width * 0.5f + APART + half);
    }
    const std::vector<float> middles =
        Spread(want, gap, area.x0 + callouts.front().width * 0.5f,
               area.x1 - callouts.back().width * 0.5f);
    const float down = side == Side::Left ? -1.0f : 1.0f;
    for (size_t i = 0; i < n; ++i)
    {
      Callout& callout = callouts[i];
      callout.row = area.Cy() + down * (REMOTE_HALF_W * scale + SPACE + ROW_H * 0.5f);
      callout.x0 = middles[i] - callout.width * 0.5f;
      callout.line = {remote.Dp(callout.anchor.at)};
      if (callout.anchor.has_bend)
        callout.line.push_back(remote.Dp(callout.anchor.bend));
      callout.line.push_back({middles[i], callout.row - down * (ROW_H * 0.5f)});
    }
    DrawCallouts(kit, callouts);
  }
}
}  // namespace

void DrawPadArt(Kit* kit, Canvas* canvas, const Rect& area, PadKind kind,
                const std::vector<PadLink>& links)
{
  kit->Begin(*canvas);
  if (kind == PadKind::WiiRemoteSideways)
  {
    const float scale =
        std::min(area.W() / (REMOTE_H + REMOTE_FRONT), area.H() / (2.0f * REMOTE_HALF_W));
    const Painter remote(kit, canvas,
                         {area.Cx() - (REMOTE_H - REMOTE_FRONT) * scale * 0.5f, area.Cy()}, scale,
                         true);
    DrawRemote(remote, {});
    DrawLinks(kit, remote, kind, SidewaysAnchors(), links);
    return;
  }

  const Plan plan = GetPlan(kind);
  const float scale = std::min(area.W() / plan.bounds.W(), area.H() / plan.bounds.H());
  const Painter painter(kit, canvas,
                        {area.Cx() - plan.bounds.Cx() * scale, area.Cy() - plan.bounds.Cy() * scale},
                        scale);
  plan.draw(painter, {});
  DrawLinks(kit, painter, kind, plan.anchors, links);
}

void DrawPad(Kit* kit, Canvas* canvas, const Rect& area, PadKind kind, const PadLabels& labels)
{
  kit->Begin(*canvas);
  if (kind == PadKind::WiiRemoteSideways)
    DrawSideways(kit, canvas, area, labels);
  else
    DrawUpright(kit, canvas, area, GetPlan(kind), labels);
}
}  // namespace VR::UI
