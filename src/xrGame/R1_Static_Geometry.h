#pragma once
#include "../Include/xrRender/RenderVisual.h"
#include "../xrCDB/R1_Static_Geometry_CRC.h"
#include "../xrPhysics/IPHWorld.h"

// Generic authoring API: all map manifests, assets and persistence stay in addons.
// Included after the normal level_script.cpp headers.
namespace
{
u32 static_geometry_version() { return 1; }

LPCSTR static_geometry_file_signature(LPCSTR name)
{
    if (!g_pGameLevel || !name ||
        (xr_strcmp(name, "level") && xr_strcmp(name, "level.geom") &&
         xr_strcmp(name, "level.geomx") && xr_strcmp(name, "level.cform") &&
         xr_strcmp(name, "level.hom")))
        return nullptr;
    // Look up the winning loose/archive resource through the engine VFS. Missing
    // resources return nil and do not turn an optional addon into a loader crash.
    if (!FS.exist("$level$", name)) return nullptr;
    IReader* reader = FS.r_open("$level$", name);
    if (!reader) return nullptr;
    const u32 size = reader->length();
    const u32 checksum = static_geometry::file_crc32(reader->pointer(), size);
    FS.r_close(reader);
    static thread_local char result[32];
    xr_sprintf(result, "%08x:%u", checksum, size);
    return result;
}

u32 static_visual_count()
{
    return g_pGameLevel && Render ? Render->getStaticVisualCount() : 0;
}

u32 static_collision_count()
{
    return g_pGameLevel ? Level().ObjectSpace.GetStaticModel()->get_tris_count() : 0;
}

bool read_static_geometry_ids(const luabind::object& values, u32 count, xr_vector<u32>& ids)
{
    if (!values || values.type() != LUA_TTABLE)
        return false;
    luabind::object table = values;
    for (luabind::object::iterator it = table.begin(), end = table.end(); it != end; ++it)
    {
        const luabind::object value = *it;
        if (value.type() != LUA_TNUMBER || ids.size() >= count)
            return false;
        const double id = luabind::object_cast<double>(value);
        // Comparisons reject NaN/infinity too; cast only after the bounds check.
        if (!(id >= 0 && id < count) || id != floor(id))
            return false;
        ids.push_back(static_cast<u32>(id));
    }
    return true;
}

bool static_visual_enabled(u32 id)
{
    return id < static_visual_count() && Render->getVisual(id)->static_geometry_enabled();
}

bool static_collision_enabled(u32 id)
{
    return id < static_collision_count() &&
        Level().ObjectSpace.GetStaticModel()->triangle_enabled(id);
}

bool update_static_collision_state(const xr_vector<u32>& ids, bool enabled)
{
    CObjectSpace& space = Level().ObjectSpace;
    CDB::MODEL* model = space.GetStaticModel();
    const u64 previous = model->get_triangle_revision();
    if (!space.SetStaticTrianglesEnabled(ids, enabled)) return false;
    if (model->get_triangle_revision() != previous && !ids.empty())
    {
        Fbox bounds; bounds.invalidate();
        const auto* triangles = model->get_tris();
        const auto* vertices = model->get_verts();
        for (u32 id : ids)
            for (u32 corner = 0; corner != 3; ++corner)
                bounds.modify(vertices[triangles[id].verts[corner]]);
        notify_static_geometry_changed(bounds);
    }
    return true;
}

bool set_static_visuals_enabled(const luabind::object& values, bool enabled)
{
    if (!g_pGameLevel || !Render)
        return false;
    xr_vector<u32> ids;
    if (!read_static_geometry_ids(values, static_visual_count(), ids))
        return false;
    for (const u32 id : ids)
        Render->getVisual(id)->set_static_geometry_enabled(enabled);
    return true;
}

bool set_static_collision_enabled(const luabind::object& values, bool enabled)
{
    if (!g_pGameLevel)
        return false;
    xr_vector<u32> ids;
    if (!read_static_geometry_ids(values, static_collision_count(), ids))
        return false;
    return update_static_collision_state(ids, enabled);
}

bool reset_static_geometry()
{
    if (!g_pGameLevel || !Render)
        return false;
    for (u32 id = 0, count = static_visual_count(); id < count; ++id)
        Render->getVisual(id)->set_static_geometry_enabled(true);
    CObjectSpace& space = Level().ObjectSpace;
    const u64 previous = space.GetStaticModel()->get_triangle_revision();
    space.ResetStaticTriangles();
    if (previous != space.GetStaticModel()->get_triangle_revision())
        notify_static_geometry_changed(space.GetBoundingVolume());
    return true;
}

bool set_static_geometry_enabled(const luabind::object& visuals,
    const luabind::object& triangles, bool enabled)
{
    if (!g_pGameLevel || !Render)
        return false;
    xr_vector<u32> visual_ids, triangle_ids;
    if (!read_static_geometry_ids(visuals, static_visual_count(), visual_ids) ||
        !read_static_geometry_ids(triangles, static_collision_count(), triangle_ids))
        return false;
    if (!update_static_collision_state(triangle_ids, enabled))
        return false;
    for (const u32 id : visual_ids)
        Render->getVisual(id)->set_static_geometry_enabled(enabled);
    return true;
}
}
