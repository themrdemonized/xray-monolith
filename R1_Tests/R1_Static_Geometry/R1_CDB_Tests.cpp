#include "xrCore/xrCore.h"
#include "xrCDB/xrCDB.h"
#include "xrCDB/frustum.h"
#include "xrCDB/R1_Static_Geometry_CRC.h"
#include <atomic>
#include <thread>
#include <cstdio>

// Minimal console host services required by xrCore's logger. The collision
// model, tree traversal, snapshots and CRC are the real compiled engine code.
LPCSTR build_date = "R1 native unit host";
u32 build_id = 0;
LPCSTR get_modded_exes_name() { return "R1 native unit host"; }
xr_string get_modded_exes_version_string() { return "unit"; }
void printLuaStack() {} // This host has no Lua VM.
enum Console_mark;
bool is_console_mark(Console_mark) { return false; }

static unsigned checks = 0;
static void check(bool value, const char* name)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
}

int main()
{
    Core._initialize("R1_CDB_Tests", nullptr, FALSE);
    check(static_geometry::file_crc32("123456789", 9) == 0xcbf43926u, "IEEE CRC32 reference vector");
    check(static_geometry::file_crc32(nullptr, 0) == 0, "empty resource CRC32");
    {
        // Two independent squares on the same ray, distances 1 and 2.
        Fvector vertices[8];
        for (unsigned plane = 0; plane != 2; ++plane)
        {
            vertices[plane * 4].set(-1, -1, float(plane + 1));
            vertices[plane * 4 + 1].set(1, -1, float(plane + 1));
            vertices[plane * 4 + 2].set(1, 1, float(plane + 1));
            vertices[plane * 4 + 3].set(-1, 1, float(plane + 1));
        }
        CDB::TRI triangles[4]{};
        for (unsigned plane = 0; plane != 2; ++plane)
        {
            triangles[plane * 2].verts[0] = plane * 4;
            triangles[plane * 2].verts[1] = plane * 4 + 1;
            triangles[plane * 2].verts[2] = plane * 4 + 2;
            triangles[plane * 2 + 1].verts[0] = plane * 4;
            triangles[plane * 2 + 1].verts[1] = plane * 4 + 2;
            triangles[plane * 2 + 1].verts[2] = plane * 4 + 3;
        }
        CDB::MODEL model, untouched;
        model.build(vertices, 8, triangles, 4);
        untouched.build(vertices, 8, triangles, 4);
        CDB::COLLIDER query;
        Fvector start, direction, center, extent;
        start.set(0.25f, 0, 0); direction.set(0, 0, 1);
        center.set(0, 0, 1.5f); extent.set(2, 2, 2);
        query.ray_options(CDB::OPT_ONLYNEAREST);
        query.ray_query(&model, start, direction, 4);
        check(query.r_count() == 1 && fsimilar(query.r_begin()->range, 1.f), "baseline nearest");
        const u64 revision = model.get_triangle_revision();
        xr_vector<u32> near_ids, far_ids, bad_ids;
        near_ids.push_back(0); near_ids.push_back(1);
        far_ids.push_back(2); far_ids.push_back(3);
        bad_ids.push_back(0); bad_ids.push_back(4);
        check(!model.set_triangles_enabled(bad_ids, false) && model.triangle_enabled(0), "all-or-nothing bad id");
        check(model.get_triangle_revision() == revision, "invalid call preserves generation");
        check(model.set_triangles_enabled(near_ids, false), "hide near plane");
        check(model.get_triangle_revision() != revision, "generation changes");
        query.ray_query(&model, start, direction, 4);
        check(query.r_count() == 1 && fsimilar(query.r_begin()->range, 2.f), "nearest passes removed plane");
        query.ray_options(CDB::OPT_ONLYFIRST);
        query.ray_query(&model, start, direction, 4);
        check(query.r_count() == 1 && query.r_begin()->id >= 2, "first skips removed plane");
        query.box_options(0);
        query.box_query(&model, center, extent);
        check(query.r_count() == 2, "box excludes removed plane");
        query.box_options(CDB::OPT_ONLYFIRST);
        query.box_query(&model, center, extent);
        check(query.r_count() == 1 && query.r_begin()->id >= 2, "box first skips removed plane");
#ifdef R1_MT
        Fobb obb;
        obb.identity(); obb.m_translate.set(center); obb.m_halfsize.set(extent);
        query.box_options(CDB::OPT_FULL_TEST);
        query.obb_query(&model, obb);
        check(query.r_count() == 2, "MT OBB excludes removed plane");
        query.box_options(CDB::OPT_FULL_TEST | CDB::OPT_ONLYFIRST);
        query.obb_query(&model, obb);
        check(query.r_count() == 1 && query.r_begin()->id >= 2, "MT OBB first skips removed plane");
#endif
        CFrustum frustum;
        frustum._clear(); // all space, no planes
        query.frustum_options(0);
        query.frustum_query(&model, frustum);
        check(query.r_count() == 2, "frustum excludes removed plane");
        const auto snapshot = model.get_triangle_state();
        check(model.set_triangles_enabled(far_ids, false), "hide far plane");
        query.ray_query(&model, start, direction, 4);
        check(query.r_count() == 0, "all planes hidden");
        check(snapshot->enabled[2] == 1, "prior snapshot remains immutable");
        query.ray_query(&untouched, start, direction, 4);
        check(query.r_count() == 1, "other model unchanged");
        model.reset_triangles();
        query.ray_options(CDB::OPT_ONLYNEAREST);
        query.ray_query(&model, start, direction, 4);
        check(query.r_count() == 1 && fsimilar(query.r_begin()->range, 1.f), "reset restores baseline");
        const u64 same = model.get_triangle_revision();
        check(model.set_triangles_enabled(near_ids, true) && model.get_triangle_revision() == same, "no-op preserves generation");
        std::atomic<unsigned> failures{0};
        std::thread reader([&]() {
            CDB::COLLIDER concurrent;
            concurrent.ray_options(CDB::OPT_ONLYNEAREST);
            for (unsigned i = 0; i != 10000; ++i)
            {
                concurrent.ray_query(&model, start, direction, 4);
                if (concurrent.r_count() != 1 ||
                    !(fsimilar(concurrent.r_begin()->range, 1.f) || fsimilar(concurrent.r_begin()->range, 2.f)))
                    ++failures;
            }
        });
        for (unsigned i = 0; i != 500; ++i)
            model.set_triangles_enabled(near_ids, (i & 1) != 0);
        reader.join();
        check(failures == 0, "concurrent snapshot queries");
    }
    std::printf("R1_CDB_TESTS PASS checks=%u concurrent_queries=10000\n", checks);
    Core._destroy();
    return 0;
}
