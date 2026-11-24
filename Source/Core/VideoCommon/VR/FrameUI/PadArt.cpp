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

uint32_t Mix(uint32_t a, uint32_t b, float t)
{
  const auto channel = [&](int shift) {
    const float from = static_cast<float>((a >> shift) & 255);
    const float to = static_cast<float>((b >> shift) & 255);
    return static_cast<uint32_t>(from + (to - from) * t + 0.5f) << shift;
  };
  return channel(0) | channel(8) | channel(16);
}

uint32_t Lighter(uint32_t rgb, float t)
{
  return Mix(rgb, 0xFFFFFF, t);
}

uint32_t Darker(uint32_t rgb, float t)
{
  return Mix(rgb, 0x000000, t);
}

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

// One box blur pass over `count` lines of `length` values.
void BoxBlur(Mask* mask, int count, int length, int line_step, int step, int radius,
             std::vector<float>* scratch)
{
  const float scale = 1.0f / static_cast<float>(2 * radius + 1);
  scratch->resize(static_cast<size_t>(length));
  float* const in = scratch->data();
  for (int line = 0; line < count; ++line)
  {
    float* const v = mask->a.data() + static_cast<ptrdiff_t>(line) * line_step;
    for (int i = 0; i < length; ++i)
      in[i] = v[static_cast<ptrdiff_t>(i) * step];
    float sum = 0.0f;
    for (int i = 0; i < std::min(radius, length); ++i)
      sum += in[i];
    for (int i = 0; i < length; ++i)
    {
      if (i + radius < length)
        sum += in[i + radius];
      v[static_cast<ptrdiff_t>(i) * step] = sum * scale;
      if (i - radius >= 0)
        sum -= in[i - radius];
    }
  }
}

// Two box blurs, close to a Gaussian of this deviation.
void Blur(Mask* mask, float deviation)
{
  const int radius = std::max(1, static_cast<int>(std::lround(deviation)));
  std::vector<float> scratch;
  for (int i = 0; i < 2; ++i)
  {
    BoxBlur(mask, mask->h, mask->w, mask->w, 1, radius, &scratch);
    BoxBlur(mask, mask->w, mask->h, 1, mask->w, radius, &scratch);
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
  // A pixel, in units (hairlines).
  float Hair() const { return 1.0f / UnitPx(); }

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

  // A gradient down the shape as it is on screen; `slant` leans it to the right.
  void Shade(const Contours& shape, uint32_t top, uint32_t bottom, float slant = 0.0f) const
  {
    const Mask mask = Cover(shape);
    const float total = 1.0f + slant;
    const float dx = slant / (static_cast<float>(mask.w) * total);
    const float dy = 1.0f / (static_cast<float>(mask.h) * total);
    const Color c0 = Rgb(top);
    const Color c1 = Rgb(bottom);
    for (int y = 0; y < mask.h; ++y)
    {
      const float* const row = mask.Row(y);
      for (int x = 0; x < mask.w; ++x)
      {
        if (row[x] <= 0.002f)
          continue;
        const float t = static_cast<float>(x) * dx + static_cast<float>(y) * dy;
        const Color c{c0.r + (c1.r - c0.r) * t, c0.g + (c1.g - c0.g) * t, c0.b + (c1.b - c0.b) * t,
                      1.0f};
        Put(mask.x0 + x, mask.y0 + y, c, row[x]);
      }
    }
  }

  // Soft light or shade inside the shape, along the edges that (dx, dy) points away from.
  void Inner(const Contours& shape, float dx, float dy, float blur, uint32_t rgb,
             float opacity) const
  {
    const float k = UnitPx();
    const Mask mask = Cover(shape);
    Mask soft = Cover(shape, std::ceil(blur * k * 2.0f) + 1.0f);
    Blur(&soft, blur * k);
    const int ox = static_cast<int>(std::lround(dx * k));
    const int oy = static_cast<int>(std::lround(dy * k));
    const Color color = Rgb(rgb, opacity);
    for (int y = 0; y < mask.h; ++y)
    {
      const float* const row = mask.Row(y);
      for (int x = 0; x < mask.w; ++x)
      {
        if (row[x] <= 0.002f)
          continue;
        const float lit = 1.0f - soft.At(mask.x0 + x - ox, mask.y0 + y - oy);
        if (lit > 0.004f)
          Put(mask.x0 + x, mask.y0 + y, color, row[x] * lit);
      }
    }
  }

  // A soft shadow (or glow) of a shape.
  void Drop(const Contours& shape, float dx, float dy, float blur, uint32_t rgb,
            float opacity) const
  {
    const float k = UnitPx();
    Mask mask = Cover(shape, std::ceil(blur * k * 2.0f) + 1.0f);
    Blur(&mask, blur * k);
    mask.x0 += static_cast<int>(std::lround(dx * k));
    mask.y0 += static_cast<int>(std::lround(dy * k));
    Paint(mask, rgb, opacity);
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
  void StrokeDashed(const Contour& line, float width, float dash, uint32_t rgb,
                    float opacity) const
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
    Stroke(pieces, width, rgb, opacity);
  }

  // Text centred on a point, upright, `size` units per em.
  void Letter(Point p, float size, uint32_t rgb, const std::string& text,
              float opacity = 1.0f) const
  {
    const Point at = Dp(p);
    const float size_dp = size * m_scale;
    const float cap = m_kit->CapHeightSized(size_dp, true);
    m_kit->TextSized(at.x, at.y + cap * 0.5f, size_dp, true, rgb, text, 1, opacity);
  }

  // A letter pressed into a surface.
  void Engrave(Point p, float size, uint32_t ink, const std::string& text) const
  {
    Letter({p.x, p.y + Hair()}, size, 0xFFFFFF, text, 0.35f);
    Letter(p, size, ink, text);
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

// A raised, rounded key of one colour.
void Key(const Painter& p, const Contours& shape, uint32_t rgb, float depth)
{
  p.Drop(shape, 0.0f, depth * 0.6f, depth * 0.6f, 0x000000, 0.55f);
  p.Shade(shape, Lighter(rgb, 0.16f), Darker(rgb, 0.10f), 0.2f);
  p.Inner(shape, 0.0f, depth * 0.45f, depth * 0.40f, 0xFFFFFF, 0.55f);
  p.Inner(shape, 0.0f, -depth * 0.55f, depth * 0.50f, 0x000000, 0.42f);
  p.Stroke(shape, 0.8f * p.Hair(), Darker(rgb, 0.55f), 0.7f, true);
}

// A hollow a control sits in.
void Dish(const Painter& p, const Contours& shape, uint32_t rgb, float depth)
{
  p.Shade(shape, Darker(rgb, 0.22f), Lighter(rgb, 0.04f));
  p.Inner(shape, 0.0f, depth * 0.6f, depth * 0.6f, 0x000000, 0.50f);
  p.Inner(shape, 0.0f, -depth * 0.3f, depth * 0.35f, 0xFFFFFF, 0.28f);
}

// --- The GameCube controller ---

constexpr float GC_CX = 433.5f;
constexpr Point GC_STICK{212.0f, 298.0f};
constexpr Point GC_DPAD{317.0f, 443.0f};
constexpr Point GC_CSTICK{550.0f, 441.0f};
constexpr Point GC_A{657.0f, 304.0f};
constexpr Point GC_B{573.0f, 343.0f};
constexpr Point GC_START{433.5f, 284.0f};

void DrawGameCube(const Painter& p)
{
  constexpr uint32_t BODY = 0x6562B4;
  constexpr uint32_t GREY = 0xC3C5CF;

  // The cable.
  p.Shade(p.Path("M 420 64 L 424 36 L 443 36 L 447 64"), 0x23242B, 0x3A3B45);
  for (const float y : {41.0f, 48.0f, 55.0f})
    p.Stroke(p.Line({425.0f, y}, {442.0f, y}), 2.0f, 0x111217, 0.8f);

  // L and R, behind the body.
  for (const bool right : {false, true})
  {
    const Contours trigger = p.Path("M 130 186 C 128 128 158 86 206 84 C 244 83 262 100 268 122 "
                                    "L 262 150 L 150 196",
                                    right, GC_CX);
    p.Shade(trigger, 0xD6D8E0, 0x8B8D99, 0.2f);
    p.Inner(trigger, right ? -5.0f : 5.0f, 6.0f, 7.0f, 0xFFFFFF, 0.6f);
    p.Inner(trigger, 0.0f, -10.0f, 12.0f, 0x202030, 0.5f);
    p.Engrave({right ? 2.0f * GC_CX - 190.0f : 190.0f, 111.0f}, 34.0f, 0x6A6C78,
              right ? "R" : "L");
  }

  // The grips, under the face.
  for (const bool right : {false, true})
  {
    const Contours grip = p.Path("M 80 285 C 74 350 78 440 95 493 C 108 532 157 536 176 500 "
                                 "C 190 472 198 440 216 412 L 250 300",
                                 right, GC_CX);
    p.Shade(grip, Darker(BODY, 0.10f), Darker(BODY, 0.36f));
    p.Inner(grip, right ? -9.0f : 9.0f, 0.0f, 10.0f, Lighter(BODY, 0.5f), 0.35f);
    p.Inner(grip, right ? 14.0f : -14.0f, -10.0f, 16.0f, 0x0C0B26, 0.55f);
  }

  // The face.
  constexpr std::string_view HALF =
      "M 433.5 58 C 375 58 330 68 300 84 C 240 116 185 138 142 163 C 98 190 80 240 80 296 "
      "C 80 372 132 428 204 433 C 232 435 240 442 246 462 C 256 499 288 517 322 517 "
      "C 370 517 407 481 407 436 C 407 396 382 374 364 352 C 352 337 354 330 370 327 "
      "C 392 322 412 320.5 433.5 320.5";
  Contour face = p.Path(HALF)[0];
  const Contour mirrored = p.Path(HALF, true, GC_CX)[0];
  face.insert(face.end(), mirrored.rbegin(), mirrored.rend());
  const Contours body{face};
  p.Drop(body, 0.0f, 8.0f, 8.0f, 0x05041A, 0.55f);
  p.Shade(body, 0x7F7CCB, 0x504D9C);
  p.Inner(body, 0.0f, 10.0f, 12.0f, 0xC4C2F4, 0.55f);
  p.Inner(body, 0.0f, -12.0f, 14.0f, 0x15133F, 0.50f);
  p.Stroke(body, 1.6f, 0x2A285C, 0.8f, true);

  // The crease between the raised middle and the sides.
  for (const bool right : {false, true})
  {
    p.Stroke(p.Path("M 300 98 C 240 130 190 150 150 173 C 130 185 118 200 112 216", right, GC_CX),
             2.5f, 0x35336F, 0.55f);
    p.Stroke(p.Path("M 301 102 C 241 134 191 154 151 177 C 131 189 120 204 114 220", right, GC_CX),
             2.0f, 0xB0AEEA, 0.45f);
  }

  // Z, on the right shoulder.
  const Contours z = p.Path("M 617 132 C 622 116 650 120 682 131 C 715 143 740 160 744 176 "
                            "C 746 186 737 189 728 184 C 696 166 660 152 626 145 "
                            "C 618 143 615 138 617 132");
  Key(p, z, 0x3A37C0, 9.0f);
  p.Letter({666.0f, 140.0f}, 25.0f, 0xC4C2FA, "Z");

  // The control stick.
  Dish(p, p.Circle(GC_STICK, 90.0f), BODY, 12.0f);
  const Contours gate = p.Octagon(GC_STICK, 66.0f, 10.0f);
  p.Shade(gate, 0x1C1B3C, 0x34326B);
  p.Inner(gate, 0.0f, 6.0f, 6.0f, 0x000000, 0.6f);
  p.Stroke(gate, 3.0f, 0x8F8DD4, 0.8f, true);
  Key(p, p.Circle(GC_STICK, 47.0f), GREY, 12.0f);
  for (const float r : {34.0f, 24.0f, 14.0f})
  {
    p.Stroke(p.Circle({GC_STICK.x, GC_STICK.y + 1.5f}, r), 2.4f, 0xFFFFFF, 0.6f, true);
    p.Stroke(p.Circle(GC_STICK, r), 2.4f, 0x80828E, 0.9f, true);
  }

  // The D-pad.
  Dish(p, p.Circle(GC_DPAD, 59.0f), BODY, 10.0f);
  Key(p, p.Cross(GC_DPAD, 47.0f, 16.0f, 6.0f), GREY, 9.0f);
  for (int i = 0; i < 4; ++i)
  {
    const Point d = Direction(static_cast<float>(i) * PI * 0.5f);
    const Point n{-d.y, d.x};
    p.Fill(p.Rounded({GC_DPAD + d * 38.0f, GC_DPAD + d * 26.0f + n * 8.0f,
                      GC_DPAD + d * 26.0f - n * 8.0f},
                     2.0f),
           0x8A8C98);
  }
  p.Fill(p.Circle(GC_DPAD, 8.0f), 0x9A9CA8);
  p.Inner(p.Circle(GC_DPAD, 8.0f), 0.0f, 3.0f, 2.0f, 0x000000, 0.35f);

  // The C stick.
  Dish(p, p.Circle(GC_CSTICK, 70.0f), BODY, 10.0f);
  const Contours c_gate = p.Octagon(GC_CSTICK, 59.0f, 9.0f);
  p.Shade(c_gate, 0xB88A08, 0xE0B21A);
  p.Inner(c_gate, 0.0f, 8.0f, 7.0f, 0x3A2A00, 0.65f);
  p.Stroke(c_gate, 2.4f, 0xF4D04A, 0.9f, true);
  Key(p, p.Circle(GC_CSTICK, 36.0f), 0xF3C21E, 11.0f);
  p.Stroke(p.Circle(GC_CSTICK, 27.0f), 2.0f, 0xB88A08, 0.7f, true);
  p.Engrave(GC_CSTICK, 38.0f, 0x8A6500, "C");

  // A, B, X, Y.
  Key(p, p.Bean(GC_A, 79.0f, 21.0f, -PI * 0.71f, -PI * 0.485f), GREY, 10.0f);
  Key(p, p.Bean(GC_A, 81.0f, 21.0f, -PI * 0.185f, PI * 0.045f), GREY, 10.0f);
  Key(p, p.Circle(GC_A, 45.0f), 0x2FB59B, 13.0f);
  Key(p, p.Circle(GC_B, 29.0f), 0xD8323C, 10.0f);
  p.Engrave(GC_A, 48.0f, 0x0B5B4C, "A");
  p.Engrave(GC_B, 34.0f, 0x650A12, "B");
  p.Engrave({GC_A.x - 24.0f, GC_A.y - 75.0f}, 32.0f, 0x5E606C, "Y");
  p.Engrave({GC_A.x + 79.0f, GC_A.y - 17.0f}, 32.0f, 0x5E606C, "X");

  // Start.
  Dish(p, p.Circle(GC_START, 24.0f), BODY, 6.0f);
  Key(p, p.Circle(GC_START, 18.0f), GREY, 7.0f);
}

// --- The Wii Remote: upright, its top centre at (0, 0) ---

constexpr float REMOTE_HALF_W = 97.0f;
constexpr float REMOTE_H = 750.0f;
constexpr uint32_t WII_INK = 0x7C8089;

void WiiKey(const Painter& p, const Contours& shape, float depth)
{
  p.Drop(shape, 0.0f, depth * 0.5f, depth * 0.6f, 0x1A2030, 0.50f);
  p.Shade(shape, 0xFFFFFF, 0xD3D7DF, 0.2f);
  p.Inner(shape, 0.0f, -depth * 0.5f, depth * 0.5f, 0x3A4254, 0.35f);
  p.Stroke(shape, p.Hair(), 0x8E94A0, 0.9f, true);
}

void DrawRemote(const Painter& p)
{
  const Contours body =
      p.Path("M -67 2 C -40 -2 40 -2 67 2 C 88 4 97 14 97 34 L 97 716 C 97 740 86 750 64 750 "
             "L -64 750 C -86 750 -97 740 -97 716 L -97 34 C -97 14 -88 4 -67 2");
  p.Shade(body, 0xFAFBFD, 0xD6DAE2, 0.6f);
  p.Inner(body, 5.0f, 5.0f, 5.0f, 0xFFFFFF, 0.9f);
  p.Inner(body, -8.0f, -8.0f, 9.0f, 0x5C667C, 0.45f);
  p.Stroke(body, p.Hair(), 0x9AA0AC, 0.8f, true);

  // The pointer's window.
  p.Shade(p.Path("M -60 4 C -30 0 30 0 60 4 C 66 5 66 18 58 19 C 30 22 -30 22 -58 19 "
                 "C -66 18 -66 5 -60 4"),
          0x3A3F4A, 0x14161B);
  p.Stroke(p.Path("M -48 9 C -24 6 24 6 48 9"), 2.5f, 0x8D96A8, 0.7f);

  // Power.
  const Point power{-56.0f, 60.0f};
  WiiKey(p, p.Circle(power, 18.0f), 6.0f);
  const Painter power_icon = p.UprightAt(power);
  power_icon.Stroke(power_icon.Arc({}, 8.0f, -PI * 0.28f, PI * 1.28f), 2.6f, 0xDA3A34);
  power_icon.Stroke(power_icon.Line({0.0f, -11.0f}, {0.0f, -2.0f}), 2.6f, 0xDA3A34);

  // B, on the back: its outline shows through.
  const Contours trigger = p.Rounded({{-58.0f, 190.0f},
                                      {58.0f, 190.0f},
                                      {66.0f, 212.0f},
                                      {54.0f, 324.0f},
                                      {-54.0f, 324.0f},
                                      {-66.0f, 212.0f}},
                                     18.0f);
  p.Fill(trigger, 0x6A7488, 0.16f);
  p.StrokeDashed(trigger[0], 2.4f, 7.0f, 0x5F687A, 0.9f);

  p.Letter({0.0f, 303.0f}, 25.0f, 0x5F687A, "B");

  // The D-pad.
  const Point dpad{0.0f, 128.0f};
  WiiKey(p, p.Cross(dpad, 50.0f, 17.0f, 5.0f), 8.0f);
  for (int i = 0; i < 4; ++i)
  {
    const Point d = Direction(static_cast<float>(i) * PI * 0.5f);
    p.Stroke(p.Line(dpad + d * 28.0f, dpad + d * 39.0f), 3.0f, 0xA2A7B1);
  }
  p.Stroke(p.Circle(dpad, 9.0f), 2.0f, 0xC2C6CE, 1.0f, true);

  // A.
  const Point a{0.0f, 246.0f};
  WiiKey(p, p.Circle(a, 37.0f), 9.0f);
  p.Stroke(p.Circle(a, 31.0f), 1.6f, 0xC4C9D2, 1.0f, true);
  p.Letter(a, 42.0f, WII_INK, "A");

  // Minus, Home, Plus.
  const Point minus{-58.0f, 367.0f};
  const Point home{0.0f, 367.0f};
  const Point plus{58.0f, 367.0f};
  for (const Point c : {minus, home, plus})
    WiiKey(p, p.Circle(c, 21.0f), 7.0f);
  const Painter minus_icon = p.UprightAt(minus);
  minus_icon.Stroke(minus_icon.Line({-8.0f, 0.0f}, {8.0f, 0.0f}), 3.4f, WII_INK);
  const Painter plus_icon = p.UprightAt(plus);
  plus_icon.Stroke(plus_icon.Line({-8.0f, 0.0f}, {8.0f, 0.0f}), 3.4f, WII_INK);
  plus_icon.Stroke(plus_icon.Line({0.0f, -8.0f}, {0.0f, 8.0f}), 3.4f, WII_INK);
  const Painter home_icon = p.UprightAt(home);
  home_icon.Fill(home_icon.Path("M 0 -13 L 13 -1 L 9 -1 L 9 10 L 3 10 L 3 2 L -3 2 L -3 10 "
                                "L -9 10 L -9 -1 L -13 -1"),
                 0x3FA4DC);

  // The speaker.
  for (int row = 0; row < 6; ++row)
  {
    for (int column = 0; column < 4; ++column)
    {
      p.Fill(p.Circle({-18.0f + 12.0f * static_cast<float>(column),
                       428.0f + 13.0f * static_cast<float>(row)},
                      3.3f),
             0x3C414C);
    }
  }

  // 1 and 2.
  for (const bool two : {false, true})
  {
    const Point c{0.0f, two ? 620.0f : 553.0f};
    WiiKey(p, p.Circle(c, 26.0f), 8.0f);
    p.Letter(c, 33.0f, WII_INK, two ? "2" : "1");
  }

  // The player lights.
  for (int i = 0; i < 4; ++i)
  {
    const float x = -54.0f + 36.0f * static_cast<float>(i);
    const Contours light = p.RoundRect(x - 6.0f, 682.0f, x + 6.0f, 694.0f, 2.0f);
    if (i == 0)
      p.Drop(light, 0.0f, 0.0f, 6.0f, 0x2FA8FF, 0.8f);
    p.Fill(light, i == 0 ? 0x37B4FF : 0x30343D);
  }
}

// --- The Nunchuk: a little from its side, its stick to the right ---

constexpr Point NUNCHUK_STICK{396.0f, 284.0f};

void DrawNunchuk(const Painter& p)
{
  WiiKey(p, p.Path("M 258 224 C 244 206 252 180 276 178 C 290 177 300 186 302 196"), 8.0f);
  WiiKey(p,
         p.Path("M 268 218 L 240 226 C 234 226 232 230 231 236 C 228 256 220 264 211 276 "
                "C 207 282 208 288 213 292 C 226 302 240 312 256 330"),
         9.0f);

  const Contours body =
      p.Path("M 340 166 C 388 166 432 196 452 242 C 474 300 480 372 472 452 C 462 542 442 640 416 "
             "698 C 406 722 392 732 374 732 C 345 732 310 702 295 652 C 283 606 286 546 290 500 "
             "C 294 440 298 402 284 364 C 272 334 246 312 242 288 C 240 264 250 236 262 216 "
             "C 284 184 310 166 340 166");
  p.Shade(body, 0xFAFBFD, 0xCFD4DD, 0.5f);
  p.Inner(body, 6.0f, 6.0f, 6.0f, 0xFFFFFF, 0.9f);
  p.Inner(body, -10.0f, -8.0f, 11.0f, 0x5C667C, 0.50f);
  p.Stroke(body, p.Hair(), 0x9AA0AC, 0.8f, true);

  // The seam between the halves.
  p.Stroke(p.Path("M 288 186 C 310 262 352 400 366 540 C 372 606 368 672 374 731"), 4.0f, 0x8C93A0,
           0.9f);
  p.Stroke(p.Path("M 292 184 C 314 260 356 400 370 540 C 376 606 372 672 378 731"), 2.0f, 0xFFFFFF,
           0.9f);

  // The stick.
  const Contours gate = p.Octagon(NUNCHUK_STICK, 57.0f, 9.0f);
  p.Shade(gate, 0x555B68, 0xA9AFBB);
  p.Inner(gate, 0.0f, 6.0f, 6.0f, 0x000000, 0.5f);
  p.Stroke(gate, 2.0f, 0xFFFFFF, 0.9f, true);
  WiiKey(p, p.Circle(NUNCHUK_STICK, 39.0f), 10.0f);
  p.Stroke(p.Circle(NUNCHUK_STICK, 30.0f), 2.2f, 0xB4B9C3, 1.0f, true);
  p.Inner(p.Circle(NUNCHUK_STICK, 29.0f), 0.0f, 4.0f, 5.0f, 0x3A4254, 0.30f);
}

// The Nunchuk and the remote together: where each's own (0, 0) is.
constexpr Point NUNCHUK_AT{-211.0f, 12.0f};
constexpr Point REMOTE_AT{415.0f, 0.0f};

void DrawRemoteAndNunchuk(const Painter& p)
{
  const Contours cord = p.Path("M 163 740 C 160 800 200 834 270 832 C 340 830 412 838 415 770");
  p.Stroke(cord, 13.0f, 0x7E8491);
  p.Stroke(cord, 10.0f, 0xC9CDD6);
  p.Stroke(cord, 3.5f, 0xF6F7FA, 0.9f);
  p.Shade(p.RoundRect(381.0f, 742.0f, 449.0f, 784.0f, 12.0f), 0xD3D7DF, 0xA4AAB6);
  p.Shade(p.RoundRect(149.0f, 734.0f, 177.0f, 774.0f, 8.0f), 0xE4E7ED, 0xB4BAC6);
  for (const float y : {752.0f, 760.0f, 768.0f})
    p.Stroke(p.Line({153.0f, y}, {173.0f, y}), 2.4f, 0x8C93A0);

  // The scale is the painter's, the place the part's.
  DrawNunchuk(p.UprightAt(NUNCHUK_AT));
  DrawRemote(p.UprightAt(REMOTE_AT));
}

// --- Callouts ---

enum class Side
{
  Left,
  Right,
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
  std::string label;
  std::string name;
  std::vector<Point> line;  // from the control to the text
  float width = 0.0f;
  float row = 0.0f;   // the middle of its text
  float edge = 0.0f;  // where its text starts, or ends
  bool flush_right = false;

  float X0() const { return flush_right ? edge - width : edge; }
  float X1() const { return flush_right ? edge : edge + width; }
};

constexpr float LABEL_GAP = 10.0f;  // between a line and its text
constexpr float STUB = 14.0f;       // the level part of a line at its text
constexpr float GUTTER = 34.0f;     // between the texts and the controller
constexpr float MAX_GUTTER = 88.0f;
constexpr float MAX_LABEL_W = 200.0f;
constexpr float ROW_H = 44.0f;
constexpr float MAX_ROW_H = 60.0f;
constexpr float LABEL_DY = -10.5f;  // the lines of text, from the row's middle
constexpr float NAME_DY = 10.5f;

const char* NameOf(PadControl control)
{
  switch (control)
  {
  case PadControl::A:
    return "A";
  case PadControl::B:
    return "B";
  case PadControl::X:
    return "X";
  case PadControl::Y:
    return "Y";
  case PadControl::Z:
  case PadControl::NunchukZ:
    return "Z";
  case PadControl::L:
    return "L";
  case PadControl::R:
    return "R";
  case PadControl::Start:
    return "Start";
  case PadControl::Stick:
  case PadControl::NunchukStick:
    return "Control Stick";
  case PadControl::CStick:
    return "C Stick";
  case PadControl::DPad:
    return "D-Pad";
  case PadControl::One:
    return "1";
  case PadControl::Two:
    return "2";
  case PadControl::Plus:
    return "Plus";
  case PadControl::Minus:
    return "Minus";
  case PadControl::Home:
    return "Home";
  case PadControl::Pointer:
    return "Pointer";
  case PadControl::Shake:
    return "Shake";
  case PadControl::C:
    return "C";
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

std::vector<Callout> MakeCallouts(const Kit& kit, const std::vector<Anchor>& anchors, Side side,
                                  const PadLabels& labels)
{
  std::vector<Callout> out;
  for (const Anchor& anchor : anchors)
  {
    const std::string& label = labels[anchor.control];
    if (anchor.side != side || label.empty())
      continue;
    Callout callout;
    callout.anchor = anchor;
    callout.label = kit.Fit(Type::Body, label, MAX_LABEL_W);
    callout.name = NameOf(anchor.control);
    callout.width = std::max(kit.TextWidth(Type::Body, callout.label),
                             kit.TextWidth(Type::Label, callout.name));
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

// Spreads the callouts of one side down a column whose text starts (or ends) at `edge`.
void PlaceColumn(std::vector<Callout>* callouts, const Painter& painter, const Rect& area,
                 Side side, float edge)
{
  const size_t n = callouts->size();
  if (n == 0)
    return;
  const float pitch = std::min(MAX_ROW_H, area.H() / static_cast<float>(n));
  const float half = pitch * static_cast<float>(n) * 0.5f;
  float middle = 0.0f;
  for (const Callout& callout : *callouts)
    middle += painter.Dp(callout.anchor.at).y / static_cast<float>(n);
  middle = std::clamp(middle, area.y0 + half, std::max(area.y0 + half, area.y1 - half));

  const float inwards = side == Side::Left ? 1.0f : -1.0f;
  const auto place = [&](size_t row) {
    Callout& callout = (*callouts)[row];
    callout.row = middle - half + pitch * (static_cast<float>(row) + 0.5f);
    callout.edge = edge;
    callout.flush_right = side == Side::Left;
    callout.line = {painter.Dp(callout.anchor.at)};
    if (callout.anchor.has_bend)
      callout.line.push_back(painter.Dp(callout.anchor.bend));
    callout.line.push_back({edge + inwards * (LABEL_GAP + STUB), callout.row});
    callout.line.push_back({edge + inwards * LABEL_GAP, callout.row});
  };

  // Rows by height, then exchanged until no two lines cross: each exchange makes the lines shorter
  // in all, so it ends.
  std::ranges::stable_sort(*callouts, [&](const Callout& a, const Callout& b) {
    return painter.Dp(a.anchor.at).y < painter.Dp(b.anchor.at).y;
  });
  for (size_t row = 0; row < n; ++row)
    place(row);
  bool changed = true;
  for (size_t pass = 0; changed && pass < n * n; ++pass)
  {
    changed = false;
    for (size_t i = 0; i < n; ++i)
    {
      for (size_t k = i + 1; k < n; ++k)
      {
        if (LinesCross((*callouts)[i].line, (*callouts)[k].line))
        {
          std::swap((*callouts)[i], (*callouts)[k]);
          place(i);
          place(k);
          changed = true;
        }
      }
    }
  }
}

void DrawCallouts(Kit* kit, const std::vector<Callout>& callouts)
{
  // The dark edges first, so that no line is cut by another's.
  for (const bool ink : {false, true})
  {
    const uint32_t rgb = ink ? Tok::kPrimary : Tok::kSurface;
    for (const Callout& callout : callouts)
    {
      for (size_t i = 0; i + 1 < callout.line.size(); ++i)
      {
        const Point a = callout.line[i];
        const Point b = callout.line[i + 1];
        kit->Line(a.x, a.y, b.x, b.y, ink ? 1.8f : 4.0f, rgb, ink ? 1.0f : 0.55f);
      }
      const Point dot = callout.line.front();
      kit->Dot(dot.x, dot.y, ink ? 4.5f : 6.2f, rgb, ink ? 1.0f : 0.85f);
    }
  }
  for (const Callout& callout : callouts)
  {
    const int align = callout.flush_right ? 2 : 0;
    kit->TextMid(callout.edge, callout.row + LABEL_DY, Type::Body, Tok::kOnSurface, callout.label,
                 align);
    kit->TextMid(callout.edge, callout.row + NAME_DY, Type::Label, Tok::kOnSurfaceVariant,
                 callout.name, align);
  }
}

// Where a diagram goes, and its callouts.
struct Placed
{
  Point origin;
  float scale = 1.0f;
  std::vector<Callout> callouts;
};

struct Plan
{
  Rect bounds;  // of the drawing, in its units
  void (*draw)(const Painter&);
  std::vector<Anchor> anchors;
};

Plan GameCubePlan()
{
  using enum PadControl;
  return {{76.0f, 34.0f, 791.0f, 528.0f},
          DrawGameCube,
          {
              {L, Side::Left, {152.0f, 126.0f}},
              {Start, Side::Left, {420.0f, 271.0f}, true, {352.0f, 200.0f}},
              {Stick, Side::Left, {167.0f, 304.0f}},
              {DPad, Side::Left, {270.0f, 443.0f}},
              {R, Side::Right, {717.0f, 120.0f}},
              {Z, Side::Right, {716.0f, 158.0f}},
              {Y, Side::Right, {672.0f, 222.0f}},
              {X, Side::Right, {757.0f, 303.0f}},
              {A, Side::Right, {692.0f, 333.0f}},
              {B, Side::Right, {601.0f, 352.0f}},
              {CStick, Side::Right, {584.0f, 454.0f}},
          }};
}

Plan RemotePlan()
{
  using enum PadControl;
  return {{-REMOTE_HALF_W, -10.0f, REMOTE_HALF_W, REMOTE_H + 10.0f},
          DrawRemote,
          {
              {Pointer, Side::Left, {-40.0f, 12.0f}},
              {DPad, Side::Left, {-50.0f, 128.0f}},
              {A, Side::Left, {-36.0f, 254.0f}},
              {Minus, Side::Left, {-78.0f, 367.0f}},
              {One, Side::Left, {-26.0f, 553.0f}},
              {B, Side::Right, {56.0f, 214.0f}},
              {Home, Side::Right, {14.0f, 351.0f}, true, {38.0f, 331.0f}},
              {Plus, Side::Right, {78.0f, 367.0f}},
              {Two, Side::Right, {26.0f, 620.0f}},
              {Shake, Side::Right, {78.0f, 712.0f}},
          }};
}

Plan NunchukPlan()
{
  using enum PadControl;
  const auto n = [](float x, float y) { return Point{x, y} + NUNCHUK_AT; };
  const auto r = [](float x, float y) { return Point{x, y} + REMOTE_AT; };
  return {{0.0f, -8.0f, 512.0f, 850.0f},
          DrawRemoteAndNunchuk,
          {
              {Pointer, Side::Left, r(-40.0f, 12.0f)},
              {DPad, Side::Left, r(-50.0f, 128.0f)},
              {C, Side::Left, n(272.0f, 194.0f)},
              {NunchukZ, Side::Left, n(232.0f, 276.0f)},
              {NunchukStick, Side::Left, n(364.0f, 306.0f)},
              {B, Side::Right, r(56.0f, 214.0f)},
              {A, Side::Right, r(36.0f, 254.0f)},
              {Home, Side::Right, r(14.0f, 351.0f), true, r(38.0f, 331.0f)},
              {Plus, Side::Right, r(78.0f, 367.0f)},
              {Minus, Side::Right, r(-48.0f, 385.0f), true, r(-22.0f, 405.0f)},
              {One, Side::Right, r(26.0f, 553.0f)},
              {Two, Side::Right, r(26.0f, 620.0f)},
              {Shake, Side::Right, r(78.0f, 712.0f)},
          }};
}

Placed PlaceUpright(Kit* kit, Canvas* canvas, const Rect& area, const Plan& plan,
                    const PadLabels& labels)
{
  std::vector<Callout> left = MakeCallouts(*kit, plan.anchors, Side::Left, labels);
  std::vector<Callout> right = MakeCallouts(*kit, plan.anchors, Side::Right, labels);
  const float left_w = WidthOf(left);
  const float right_w = WidthOf(right);
  const float room = area.W() - left_w - right_w;

  Placed placed;
  const float w = plan.bounds.W();
  const float scale =
      std::max(0.05f, std::min((room - 2.0f * GUTTER) / w, area.H() / plan.bounds.H()));
  const float gutter = std::clamp((room - w * scale) * 0.5f, 0.0f, MAX_GUTTER);
  const float left_edge = area.Cx() + (left_w - right_w - w * scale) * 0.5f - gutter;
  const float right_edge = left_edge + 2.0f * gutter + w * scale;
  placed.scale = scale;
  placed.origin = {left_edge + gutter - plan.bounds.x0 * scale,
                   area.Cy() - plan.bounds.Cy() * scale};

  const Painter painter(kit, canvas, placed.origin, scale);
  PlaceColumn(&left, painter, area, Side::Left, left_edge);
  PlaceColumn(&right, painter, area, Side::Right, right_edge);
  placed.callouts = std::move(left);
  std::ranges::move(right, std::back_inserter(placed.callouts));
  return placed;
}

void DrawUpright(Kit* kit, Canvas* canvas, const Rect& area, const Plan& plan,
                 const PadLabels& labels)
{
  const Placed placed = PlaceUpright(kit, canvas, area, plan, labels);
  plan.draw(Painter(kit, canvas, placed.origin, placed.scale));
  DrawCallouts(kit, placed.callouts);
}

// --- The remote held sideways: lines go up or down from it, then out to the texts ---

// The hands that hold the turned remote (in its units, its middle at y = 0): the palms behind its
// ends, then the thumbs on its face.
void DrawHands(const Painter& p, bool thumbs)
{
  constexpr uint32_t HAND = 0x56667D;
  for (const bool right : {false, true})
  {
    if (!thumbs)
    {
      const Contours palm =
          p.Path("M 70 -60 C 70 -112 40 -122 6 -116 C -30 -108 -48 -74 -52 -20 "
                 "C -55 40 -44 96 -10 116 C 24 130 80 124 96 100 L 96 0",
                 right, REMOTE_H * 0.5f);
      p.Shade(palm, HAND, Darker(HAND, 0.40f));
      p.Inner(palm, 0.0f, 7.0f, 7.0f, 0xFFFFFF, 0.25f);
      continue;
    }
    const Contours thumb =
        p.Path("M -44 104 C -46 62 -22 26 16 2 C 32 -8 42 -18 54 -24 C 78 -34 98 -8 84 14 "
               "C 72 32 46 46 36 70 C 31 84 32 96 30 112 C 10 126 -30 124 -44 104",
               right, REMOTE_H * 0.5f);
    p.Drop(thumb, 0.0f, 5.0f, 6.0f, 0x10141C, 0.45f);
    p.Shade(thumb, Lighter(HAND, 0.14f), Darker(HAND, 0.24f));
    p.Inner(thumb, 0.0f, 5.0f, 5.0f, 0xFFFFFF, 0.28f);
    const Contours nail =
        p.Path("M 52 -8 C 58 -22 82 -24 84 -6 C 84 4 74 12 64 14 C 54 14 48 2 52 -8", right,
               REMOTE_H * 0.5f);
    p.Shade(nail, Lighter(HAND, 0.42f), Lighter(HAND, 0.22f));
  }
}

Placed PlaceSideways(Kit* kit, Canvas* canvas, const Rect& area, const PadLabels& labels)
{
  using enum PadControl;
  // In the upright remote's units; the turn is the painter's.
  const std::vector<Anchor> above = {
      {Pointer, Side::Left, {48.0f, 12.0f}},
      {DPad, Side::Left, {50.0f, 128.0f}},
      {A, Side::Left, {37.0f, 246.0f}},
      {Plus, Side::Right, {78.0f, 367.0f}},
      {One, Side::Right, {26.0f, 553.0f}},
  };
  const std::vector<Anchor> under = {
      {B, Side::Left, {-64.0f, 225.0f}},
      {Minus, Side::Left, {-78.0f, 367.0f}},
      {Home, Side::Right, {-16.0f, 381.0f}, true, {-36.0f, 400.0f}},
      {Two, Side::Right, {-26.0f, 620.0f}},
      {Shake, Side::Right, {-82.0f, 690.0f}},
  };

  struct Block
  {
    bool below;
    bool right;
    std::vector<Callout> callouts;
  };
  std::vector<Block> blocks;
  size_t row_counts[2] = {0, 0};
  for (const bool below : {false, true})
  {
    for (const bool right : {false, true})
    {
      Block block{below, right,
                  MakeCallouts(*kit, below ? under : above, right ? Side::Right : Side::Left,
                               labels)};
      size_t& count = row_counts[below ? 1 : 0];
      count = std::max(count, block.callouts.size());
      blocks.push_back(std::move(block));
    }
  }

  constexpr float HANDS = 60.0f;  // units past the remote's ends
  constexpr float SPACE = 26.0f;  // between the remote and the nearest text
  const float rows_above = static_cast<float>(row_counts[0]);
  const float rows = rows_above + static_cast<float>(row_counts[1]);
  const float scale =
      std::max(0.05f, std::min(area.W() / (REMOTE_H + 2.0f * HANDS),
                               (area.H() - rows * ROW_H - 2.0f * SPACE) / (2.0f * REMOTE_HALF_W)));
  const float thick = 2.0f * REMOTE_HALF_W * scale;
  const float spare = area.H() - thick - 2.0f * SPACE;
  const float pitch = rows > 0.0f ? std::min(MAX_ROW_H, spare / rows) : 0.0f;
  const float top =
      area.Cy() - (thick + rows * pitch) * 0.5f + rows_above * pitch;
  Placed placed;
  placed.scale = scale;
  placed.origin = {area.Cx() - REMOTE_H * scale * 0.5f, top + thick * 0.5f};
  const Painter remote(kit, canvas, placed.origin, scale, true);

  for (Block& block : blocks)
  {
    const float out = block.right ? 1.0f : -1.0f;
    const float down = block.below ? 1.0f : -1.0f;
    // The nearest row is the outermost control's, so that no line has to pass another.
    std::ranges::stable_sort(block.callouts, [&](const Callout& a, const Callout& b) {
      return out * remote.Dp(a.anchor.at).x > out * remote.Dp(b.anchor.at).x;
    });
    // How far in the rows so far reach, measured inwards from the area's edge.
    const float edge = block.right ? area.x1 : area.x0;
    float reach = -STUB;
    float row = 0.5f;
    for (Callout& callout : block.callouts)
    {
      callout.row = (block.below ? top + thick + SPACE : top - SPACE) + down * pitch * row;
      callout.edge = edge;
      callout.flush_right = block.right;
      callout.line = {remote.Dp(callout.anchor.at)};
      if (callout.anchor.has_bend)
        callout.line.push_back(remote.Dp(callout.anchor.bend));
      // Where the line turns: over the control, or further in to pass the rows before.
      const float over = (edge - callout.line.back().x) * out;
      const float turn = std::max(over, reach + STUB);
      if (turn < callout.width + LABEL_GAP)
      {
        // Over its own text: straight onto it (onto the label, where the name is shorter).
        const bool onto_label =
            !block.below && turn > kit->TextWidth(Type::Label, callout.name) + LABEL_GAP;
        callout.line.push_back({edge - out * std::clamp(turn, 4.0f, callout.width - 4.0f),
                                callout.row - down * (onto_label ? 5.0f : 22.0f)});
      }
      else
      {
        // Aside before the texts it passes.
        if (turn > over + 0.5f)
        {
          callout.line.push_back(
              {edge - out * turn, block.below ? top + thick + SPACE * 0.4f : top - SPACE * 0.4f});
        }
        callout.line.push_back({edge - out * turn, callout.row});
        callout.line.push_back({edge - out * (callout.width + LABEL_GAP), callout.row});
      }
      reach = std::max({reach, turn, callout.width + LABEL_GAP});
      row += 1.0f;
    }
    std::ranges::move(block.callouts, std::back_inserter(placed.callouts));
  }
  return placed;
}

void DrawSideways(Kit* kit, Canvas* canvas, const Rect& area, const PadLabels& labels)
{
  const Placed placed = PlaceSideways(kit, canvas, area, labels);
  const Painter level(kit, canvas, placed.origin, placed.scale);
  DrawHands(level, false);
  DrawRemote(Painter(kit, canvas, placed.origin, placed.scale, true));
  DrawHands(level, true);
  DrawCallouts(kit, placed.callouts);
}
}  // namespace

void DrawPad(Kit* kit, Canvas* canvas, const Rect& area, PadKind kind, const PadLabels& labels)
{
  kit->Begin(*canvas);
  switch (kind)
  {
  case PadKind::GameCube:
    DrawUpright(kit, canvas, area, GameCubePlan(), labels);
    break;
  case PadKind::WiiRemote:
    DrawUpright(kit, canvas, area, RemotePlan(), labels);
    break;
  case PadKind::WiiNunchuk:
    DrawUpright(kit, canvas, area, NunchukPlan(), labels);
    break;
  case PadKind::WiiRemoteSideways:
    DrawSideways(kit, canvas, area, labels);
    break;
  }
}
}  // namespace VR::UI
