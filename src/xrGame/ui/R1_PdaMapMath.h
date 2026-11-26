#pragma once
#include <array>
#include <cmath>

namespace R1PdaMapMath
{
    struct Point { float x, y; };

    // Live UI origin, native (aspect-adjusted) BoundRect, zoom and UI aspect.
    // This is the non-rotated CUICustomMap/CUIGlobalMap coordinate contract.
    struct Transform
    {
        float origin_x, origin_y, bound_x, bound_top, bound_bottom, zoom, aspect;
        bool global;

        bool valid() const
        {
            return std::isfinite(origin_x) && std::isfinite(origin_y) && std::isfinite(bound_x) &&
                std::isfinite(bound_top) && std::isfinite(bound_bottom) &&
                std::isfinite(zoom) && std::isfinite(aspect) && zoom > 0.0001f && aspect > 0.0001f;
        }

        bool to_ui(Point src, Point& dst) const
        {
            if (!valid() || !std::isfinite(src.x) || !std::isfinite(src.y)) return false;
            dst.x = origin_x + (src.x * aspect - bound_x) * zoom;
            dst.y = origin_y + (global ? src.y - bound_top : bound_bottom - src.y) * zoom;
            return std::isfinite(dst.x) && std::isfinite(dst.y);
        }

        bool from_ui(Point src, Point& dst) const
        {
            if (!valid() || !std::isfinite(src.x) || !std::isfinite(src.y)) return false;
            dst.x = (bound_x + (src.x - origin_x) / zoom) / aspect;
            dst.y = global ? bound_top + (src.y - origin_y) / zoom
                : bound_bottom - (src.y - origin_y) / zoom;
            return std::isfinite(dst.x) && std::isfinite(dst.y);
        }
    };

    // Same winding as the standard UI textured quad. A zero-length segment is a square dot.
    inline bool line_quad(Point a, Point b, float width, std::array<Point, 4>& out)
    {
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) ||
            !std::isfinite(b.y) || !std::isfinite(width) || width <= 0.f) return false;
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float length = std::hypot(dx, dy), r = width * .5f;
        if (length <= 0.0001f)
            out = {{{a.x-r,a.y-r}, {a.x+r,a.y-r}, {a.x+r,a.y+r}, {a.x-r,a.y+r}}};
        else
        {
            const float nx = -dy * r / length, ny = dx * r / length;
            out = {{{a.x-nx,a.y-ny}, {b.x-nx,b.y-ny}, {b.x+nx,b.y+ny}, {a.x+nx,a.y+ny}}};
        }
        for (Point p : out) if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
        return true;
    }
}
