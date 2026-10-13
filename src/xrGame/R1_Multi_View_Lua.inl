// Included beside the level Lua functions; October ordinary source API.
static int r1_view_api_version() { return 1; }
template<class T> static T r1_option(const luabind::object& table, const char* key, const T& fallback)
{
    luabind::object value = table[key];
    return value.type() == LUA_TNIL ? fallback : luabind::object_cast<T>(value);
}
static bool r1_view_set(u32 id, const luabind::object& table)
{
    if (!Render || table.type() != LUA_TTABLE) return false;
    R1ViewParameters p;
#define R1OPT(name) p.name = r1_option(table, #name, p.name)
    R1OPT(width); R1OPT(height); R1OPT(interval); R1OPT(quality); R1OPT(output);
    R1OPT(relative); R1OPT(primary); R1OPT(hud); R1OPT(clip); R1OPT(flip_x); R1OPT(actor_body);
    R1OPT(position); R1OPT(direction); R1OPT(up); R1OPT(offset);
    R1OPT(quad_position); R1OPT(quad_right); R1OPT(quad_up);
    R1OPT(fov); R1OPT(aspect); R1OPT(far_distance); R1OPT(projection_shift);
#undef R1OPT
    luabind::object transform=table["camera_transform"];
    if(transform.type()!=LUA_TNIL){p.transform_camera=true;p.camera_transform=luabind::object_cast<Fmatrix>(transform);}
    p.rect.x=r1_option(table,"rect_x",0.f); p.rect.y=r1_option(table,"rect_y",0.f);
    p.rect.z=r1_option(table,"rect_w",1.f); p.rect.w=r1_option(table,"rect_h",1.f);
    p.plane.x=r1_option(table,"plane_x",0.f); p.plane.y=r1_option(table,"plane_y",0.f);
    p.plane.z=r1_option(table,"plane_z",1.f); p.plane.w=r1_option(table,"plane_d",0.f);
    return Render->R1ViewSet(id,p);
}
static void r1_view_clear(u32 id) { if (Render) Render->R1ViewClear(id); }
static bool r1_view_geometry(u32 id,const luabind::object& table)
{
    if(!Render||table.type()!=LUA_TTABLE)return false;
    R1ViewGeometry geometry;
    geometry.textures[0]=r1_option<LPCSTR>(table,"texture1","");
    geometry.textures[1]=r1_option<LPCSTR>(table,"texture2","");
    luabind::object vertices=table["vertices"];
    if(vertices.type()!=LUA_TTABLE)return false;
    for(u32 i=1;i<=256;++i)
    {
        luabind::object item=vertices[i];if(item.type()==LUA_TNIL)break;
        if(item.type()!=LUA_TTABLE)return false;
        R1ViewVertex v;v.position=luabind::object_cast<Fvector>(item["position"]);
        v.uv.set(r1_option(item,"u",0.f),r1_option(item,"v",0.f));v.material=r1_option(item,"material",0u);
        geometry.vertices.push_back(v);
    }
    return Render->R1ViewMesh(id,geometry);
}
static luabind::object r1_view_status(u32 id)
{
    luabind::object result=luabind::newtable(ai().script_engine().lua());
    R1ViewStatus s; bool valid=Render && Render->R1ViewGet(id,s);
    result["valid"]=valid; result["ready"]=s.ready; result["error"]=s.error;
    result["width"]=s.width; result["height"]=s.height; result["frame"]=s.frame;
    result["draws"]=s.draws; result["renders"]=s.renders; result["render_ms"]=s.render_ms; result["total_ms"]=s.total_ms;
    return result;
}
