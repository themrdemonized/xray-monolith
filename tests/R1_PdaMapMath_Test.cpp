#include "../src/xrGame/ui/R1_PdaMapMath.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static unsigned checks = 0;
static void check(bool condition, const char* name)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
static bool near(float a, float b) { return std::abs(a-b) < 0.002f; }
int main()
{
    using namespace R1PdaMapMath;
    // Non-zero bounds/origins, differing aspect ratios and live zoom values.
    for (bool global : {false, true})
    for (float aspect : {1.f, .75f, .5625f, 1.2f})
    for (float zoom : {.05f, .7f, 1.f, 3.6f})
    {
        Transform t{137.f,-83.f,-320.f*aspect,-450.f,1250.f,zoom,aspect,global};
        for (Point p : {Point{-319.f,-449.f}, Point{123.f,456.f}, Point{700.f,1200.f}})
        {
            Point ui, back;
            check(t.to_ui(p, ui) && t.from_ui(ui, back), "conversion succeeds");
            check(near(back.x,p.x) && near(back.y,p.y), "world/global round trip");
            Transform moved = t;
            moved.origin_x += 20.f; moved.origin_y -= 17.f;
            Point after;
            check(moved.to_ui(p, after) && near(after.x,ui.x+20.f) && near(after.y,ui.y-17.f), "live pan");
        }
    }
    Point a,b;
    Transform level{100.f,200.f,-300.f*.75f,-500.f,1000.f,2.f,.75f,false};
    check(level.to_ui({-300.f,1000.f},a) && near(a.x,100.f) && near(a.y,200.f), "level top-left");
    check(level.to_ui({-300.f,-500.f},b) && near(b.y,3200.f), "level Z inverted");
    Transform global = level; global.global = true;
    check(global.to_ui({-300.f,-500.f},a) && near(a.y,200.f), "global Y not inverted");
    for (float bad : {0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
    {
        Transform t = level; t.zoom = bad;
        check(!t.to_ui({0.f,0.f},a) && !t.from_ui({0.f,0.f},a), "invalid zoom rejected");
        t = level; t.aspect = bad;
        check(!t.to_ui({0.f,0.f},a) && !t.from_ui({0.f,0.f},a), "invalid aspect rejected");
    }
    check(!level.to_ui({std::numeric_limits<float>::quiet_NaN(),0.f},a), "NaN position rejected");
    std::array<Point,4> quad;
    check(line_quad({10.f,20.f},{30.f,20.f},4.f,quad), "horizontal segment");
    check(near(quad[0].x,10.f) && near(quad[0].y,18.f) && near(quad[2].y,22.f), "quad width and winding");
    check(line_quad({10.f,20.f},{10.f,40.f},4.f,quad), "vertical segment");
    check(near(quad[0].x,12.f) && near(quad[2].x,8.f), "vertical width");
    check(line_quad({10.f,20.f},{10.f,20.f},4.f,quad), "dot segment");
    check(near(quad[0].x,8.f) && near(quad[0].y,18.f) && near(quad[2].x,12.f), "dot bounds");
    check(!line_quad({0.f,0.f},{1.f,1.f},0.f,quad), "zero width rejected");
    check(!line_quad({0.f,0.f},{1.f,1.f},std::numeric_limits<float>::infinity(),quad), "infinite width rejected");
    std::printf("R1 PDA Map Math: %u checks passed\n",checks);
}
