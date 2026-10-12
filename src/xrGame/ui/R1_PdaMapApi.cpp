#include "stdafx.h"
#include "R1_PdaMapApi.h"
#include "R1_PdaMapMath.h"
#include "UIMapWnd.h"
#include "UIMap.h"
#include "UITextureMaster.h"
#include "../UICursor.h"
#include "../UIStaticItem.h"
#include "../ai_space.h"
#include "script_engine.h"
#include "../../xrEngine/xr_input.h"
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>

extern CUIMapWnd* GetMapWnd();

namespace R1PdaMap
{
    static constexpr u32 max_layers = 32;
    static constexpr u32 max_commands_per_layer = 4096;
    static constexpr u32 max_points = 65536;
    static constexpr u32 max_polyline_points = 4096;
    static constexpr u32 max_textures_per_layer = 128;
    static std::atomic<u32> next_token{1};

    struct Command
    {
        u32 id = 0;
        shared_str map_name;
        shared_str texture;
        xr_vector<Fvector2> points;
        Fvector2 size;
        float width = 1.f;
        u32 color = 0xffffffff;
        bool closed = false;
        bool scale = false;
    };

    struct Layer
    {
        shared_str owner;
        bool visible = true;
        xr_vector<Command> commands;
        // Created only while drawing. No Lua or resource creation on submission.
        xr_map<shared_str, std::unique_ptr<CUIStaticItem>> textures;
    };

    struct DrawVertex { S2DVert vertex; u32 color; };

    class Store
    {
    public:
        CUIMapWnd& map;
        std::mutex mutex;
        xr_map<u32, std::unique_ptr<Layer>> layers;
        u32 input_layer = 0;
        u32 point_count = 0;
        u32 next_command = 1;
        xr_vector<DrawVertex> vertices;
        explicit Store(CUIMapWnd& w) : map(w) {}
    };

    static Store* current()
    {
        CUIMapWnd* w = GetMapWnd();
        return w && w->GlobalMap() ? w->ScriptMapLayers() : nullptr;
    }

    static bool finite(float v)
    {
        return std::isfinite(v) && std::abs(v) <= 10000000.f;
    }

    static bool width_valid(float v)
    {
        return finite(v) && v > 0.f && v <= 128.f;
    }

    static CUICustomMap* find_map(Store& s, LPCSTR name)
    {
        if (!name || !name[0]) return nullptr;
        if (xr_strcmp(name, "global_map") == 0) return s.map.GlobalMap();
        const auto& maps = s.map.GameMaps();
        auto it = maps.find(shared_str(name));
        return it == maps.end() ? nullptr : it->second;
    }

    static R1PdaMapMath::Transform transform_of(Store& s, CUICustomMap& map)
    {
        Fvector2 origin;
        map.GetAbsolutePos(origin);
        const Frect& bound = map.BoundRect();
        return {origin.x, origin.y, bound.x1, bound.y1, bound.y2,
            map.GetCurrentZoom().x, UI().get_current_kx(), &map == s.map.GlobalMap()};
    }

    // Both directions use the same live rect, including smooth zoom/pan.
    static bool project(Store& s, LPCSTR name, const Fvector2& src, Fvector2& dst)
    {
        CUICustomMap* m = find_map(s, name);
        if (!m || m->Heading() || !finite(src.x) || !finite(src.y)) return false;
        R1PdaMapMath::Point result;
        if (!transform_of(s, *m).to_ui({src.x, src.y}, result)) return false;
        dst.set(result.x, result.y);
        return finite(dst.x) && finite(dst.y);
    }

    static bool unproject(Store& s, LPCSTR name, const Fvector2& src, Fvector2& dst)
    {
        CUICustomMap* m = find_map(s, name);
        if (!m || m->Heading() || !finite(src.x) || !finite(src.y)) return false;
        R1PdaMapMath::Point result;
        if (!transform_of(s, *m).from_ui({src.x, src.y}, result)) return false;
        dst.set(result.x, result.y);
        return finite(dst.x) && finite(dst.y);
    }

    static bool texture_valid(LPCSTR texture)
    {
        if (!texture || !texture[0] || xr_strlen(texture) >= 256) return false;
        TEX_INFO info = CUITextureMaster::FindItem(shared_str(texture));
        if (info.file.size()) return info.rect.width() > 0.f && info.rect.height() > 0.f;
        // Raw files must exist. A missing texture must not trigger the renderer's fatal loader.
        string_path path;
        return !!FS.exist(path, "$game_textures$", texture, ".dds");
    }

    static u32 register_layer(LPCSTR owner)
    {
        Store* s = current();
        if (!s || !owner || !owner[0] || xr_strlen(owner) > 128) return 0;
        std::lock_guard<std::mutex> lock(s->mutex);
        if (s->layers.size() >= max_layers) return 0;
        for (const auto& pair : s->layers)
            if (pair.second->owner == owner) return 0; // do not hand out another writer's token
        // Never wrap and reuse a token from a previous UI/session.
        u32 token = next_token.load();
        do { if (!token || token == u32(-1)) return 0; }
        while (!next_token.compare_exchange_weak(token, token + 1));
        std::unique_ptr<Layer> layer(new Layer());
        layer->owner = owner;
        s->layers.emplace(token, std::move(layer));
        return token;
    }

    static bool valid_layer(u32 token)
    {
        Store* s = current();
        if (!s) return false;
        std::lock_guard<std::mutex> lock(s->mutex);
        return s->layers.find(token) != s->layers.end();
    }

    static bool clear_layer(u32 token)
    {
        Store* s = current();
        if (!s) return false;
        std::lock_guard<std::mutex> lock(s->mutex);
        auto it = s->layers.find(token);
        if (it == s->layers.end()) return false;
        for (const Command& c : it->second->commands) s->point_count -= u32(c.points.size());
        it->second->commands.clear();
        it->second->textures.clear();
        return true;
    }

    static bool release_layer(u32 token)
    {
        Store* s = current();
        if (!s) return false;
        std::lock_guard<std::mutex> lock(s->mutex);
        auto it = s->layers.find(token);
        if (it == s->layers.end()) return false;
        for (const Command& c : it->second->commands) s->point_count -= u32(c.points.size());
        if (s->input_layer == token) s->input_layer = 0;
        s->layers.erase(it);
        return true;
    }

    static bool set_visible(u32 token, bool visible)
    {
        Store* s = current();
        if (!s) return false;
        std::lock_guard<std::mutex> lock(s->mutex);
        auto it = s->layers.find(token);
        if (it == s->layers.end()) return false;
        it->second->visible = visible;
        if (!visible && s->input_layer == token) s->input_layer = 0;
        return true;
    }

    static bool acquire_input(u32 token)
    {
        Store* s = current();
        if (!s || !s->map.IsShown()) return false;
        std::lock_guard<std::mutex> lock(s->mutex);
        auto it = s->layers.find(token);
        if (it == s->layers.end() || !it->second->visible || (s->input_layer && s->input_layer != token))
            return false;
        s->input_layer = token;
        return true;
    }

    static bool release_input(u32 token)
    {
        Store* s = current();
        if (!s) return false;
        std::lock_guard<std::mutex> lock(s->mutex);
        if (!token || s->input_layer != token) return false;
        s->input_layer = 0;
        return true;
    }

    static u32 append(u32 token, Command&& c)
    {
        Store* s = current();
        if (!s || !find_map(*s, c.map_name.c_str())) return 0;
        std::lock_guard<std::mutex> lock(s->mutex);
        auto it = s->layers.find(token);
        if (it == s->layers.end() || it->second->commands.size() >= max_commands_per_layer ||
            c.points.size() > max_points - s->point_count || s->next_command == u32(-1)) return 0;
        if (c.texture.size() && it->second->textures.find(c.texture) == it->second->textures.end())
        {
            // Reserve one texture slot for lines. Refuse excess marker textures at submission.
            xr_set<shared_str> ids;
            for (const auto& pair : it->second->textures) ids.insert(pair.first);
            for (const Command& old : it->second->commands) if (old.texture.size()) ids.insert(old.texture);
            ids.insert(shared_str("ui_inGame2_white_rect"));
            ids.insert(c.texture);
            if (ids.size() > max_textures_per_layer) return 0;
        }
        c.id = s->next_command++;
        s->point_count += u32(c.points.size());
        const u32 id = c.id;
        it->second->commands.push_back(std::move(c));
        return id;
    }

    static u32 add_line(u32 token, LPCSTR map, float x1, float y1, float x2, float y2, float width, u32 color)
    {
        if (!map || !finite(x1) || !finite(y1) || !finite(x2) || !finite(y2) || !width_valid(width)) return 0;
        Command c;
        c.map_name = map; c.width = width; c.color = color;
        c.points.push_back(Fvector2().set(x1, y1));
        c.points.push_back(Fvector2().set(x2, y2));
        return append(token, std::move(c));
    }

    static u32 add_polyline(u32 token, LPCSTR map, const luabind::object& points, float width, u32 color, bool closed)
    {
        if (!map || points.type() != LUA_TTABLE || !width_valid(width)) return 0;
        Command c;
        c.map_name = map; c.width = width; c.color = color; c.closed = closed;
        for (u32 i = 1; i <= max_polyline_points + 1; ++i)
        {
            luabind::object row = points[i];
            if (row.type() == LUA_TNIL) break;
            if (i > max_polyline_points || row.type() != LUA_TTABLE) return 0;
            luabind::object x = row[1], y = row[2];
            if (x.type() != LUA_TNUMBER || y.type() != LUA_TNUMBER) return 0;
            const double px = luabind::object_cast<double>(x), py = luabind::object_cast<double>(y);
            if (!std::isfinite(px) || !std::isfinite(py) || std::abs(px) > 10000000. || std::abs(py) > 10000000.) return 0;
            c.points.push_back(Fvector2().set(float(px), float(py)));
        }
        if (c.points.size() < 2 || (closed && c.points.size() < 3)) return 0;
        return append(token, std::move(c));
    }

    static u32 add_marker(u32 token, LPCSTR map, LPCSTR texture, float x, float y,
        float width, float height, u32 color, bool scale_with_map)
    {
        if (!map || !finite(x) || !finite(y) || !finite(width) || !finite(height) ||
            width <= 0.f || height <= 0.f || width > 2048.f || height > 2048.f || !texture_valid(texture)) return 0;
        Command c;
        c.map_name = map; c.texture = texture; c.color = color; c.scale = scale_with_map;
        c.size.set(width, height);
        c.points.push_back(Fvector2().set(x, y));
        return append(token, std::move(c));
    }

    static bool remove(u32 token, u32 command)
    {
        Store* s = current();
        if (!s) return false;
        std::lock_guard<std::mutex> lock(s->mutex);
        auto layer = s->layers.find(token);
        if (layer == s->layers.end()) return false;
        auto& commands = layer->second->commands;
        for (auto it = commands.begin(); it != commands.end(); ++it)
            if (it->id == command)
            {
                const shared_str old_texture = it->texture;
                s->point_count -= u32(it->points.size());
                commands.erase(it);
                if (old_texture.size())
                {
                    bool used = false;
                    for (const Command& c : commands) if (c.texture == old_texture) { used = true; break; }
                    if (!used) layer->second->textures.erase(old_texture);
                }
                return true;
            }
        return false;
    }

    static u32 count(u32 token)
    {
        Store* s = current();
        if (!s) return 0;
        std::lock_guard<std::mutex> lock(s->mutex);
        auto it = s->layers.find(token);
        return it == s->layers.end() ? 0 : u32(it->second->commands.size());
    }

    static bool to_screen(LPCSTR map, float x, float y, float& sx, float& sy)
    {
        sx = sy = 0.f;
        Store* s = current();
        Fvector2 result;
        if (!s || !project(*s, map, Fvector2().set(x, y), result)) return false;
        sx = result.x; sy = result.y;
        return true;
    }

    static bool from_screen(LPCSTR map, float x, float y, float& mx, float& my)
    {
        mx = my = 0.f;
        Store* s = current();
        Fvector2 result;
        if (!s || !unproject(*s, map, Fvector2().set(x, y), result)) return false;
        mx = result.x; my = result.y;
        return true;
    }

    static luabind::object viewport(lua_State* L)
    {
        auto row = luabind::newtable(L);
        Store* s = current();
        row["ready"] = s != nullptr;
        if (!s) return row;
        const Frect rect = s->map.ActiveMapRect();
        row["shown"] = s->map.IsShown();
        row["x"] = rect.x1; row["y"] = rect.y1;
        row["width"] = rect.width(); row["height"] = rect.height();
        row["zoom"] = s->map.GlobalMap()->GetCurrentZoom().x;
        return row;
    }

    static LPCSTR action_name(int action)
    {
        switch (action)
        {
        case WINDOW_LBUTTON_DOWN: return "left_down";
        case WINDOW_LBUTTON_UP: return "left_up";
        case WINDOW_LBUTTON_DB_CLICK: return "left_double_click";
        case WINDOW_RBUTTON_DOWN: return "right_down";
        case WINDOW_RBUTTON_UP: return "right_up";
        case WINDOW_CBUTTON_DOWN: return "middle_down";
        case WINDOW_CBUTTON_UP: return "middle_up";
        case WINDOW_MOUSE_MOVE: return "mouse_move";
        case WINDOW_MOUSE_WHEEL_UP: return "wheel_up";
        case WINDOW_MOUSE_WHEEL_DOWN: return "wheel_down";
        case WINDOW_KEY_PRESSED: return "key_down";
        case WINDOW_KEY_RELEASED: return "key_up";
        default: return nullptr;
        }
    }

    bool input(Store& s, int action, int dik)
    {
        const LPCSTR name = action_name(action);
        if (!name) return false;
        const Fvector2 cursor = GetUICursor().GetCursorPosition();
        if (!s.map.ScriptInputAllowed(cursor)) return false;
        shared_str owner;
        u32 token;
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            token = s.input_layer;
            auto it = s.layers.find(token);
            if (it == s.layers.end() || !it->second->visible) return false;
            owner = it->second->owner;
        }
        luabind::functor<void> callback;
        if (!ai().script_engine().functor("_G.SendScriptCallback", callback)) return false;
        lua_State* L = ai().script_engine().lua();
        auto event = luabind::newtable(L);
        event["owner"] = owner.c_str(); event["layer"] = token;
        event["action"] = name; event["dik"] = dik;
        event["screen_x"] = cursor.x; event["screen_y"] = cursor.y;
        event["shift"] = !!(pInput->iGetAsyncKeyState(DIK_LSHIFT) || pInput->iGetAsyncKeyState(DIK_RSHIFT));
        event["ctrl"] = !!(pInput->iGetAsyncKeyState(DIK_LCONTROL) || pInput->iGetAsyncKeyState(DIK_RCONTROL));
        event["alt"] = !!(pInput->iGetAsyncKeyState(DIK_LMENU) || pInput->iGetAsyncKeyState(DIK_RMENU));
        event["left_held"] = !!pInput->iGetAsyncBtnState(0);
        event["consume"] = false;
        Fvector2 global;
        if (unproject(s, "global_map", cursor, global))
        {
            event["global_x"] = global.x; event["global_y"] = global.y;
        }
        // Resolve the hovered level from current rects, not the previous frame's hover pointer.
        for (const auto& pair : s.map.GameMaps())
        {
            Frect r;
            pair.second->GetAbsoluteRect(r);
            Fvector2 pos;
            if (pair.second->IsShown() && r.in(cursor.x, cursor.y) && unproject(s, pair.first.c_str(), cursor, pos))
            {
                event["level_name"] = pair.first.c_str();
                event["world_x"] = pos.x; event["world_z"] = pos.y;
                break;
            }
        }
        // No store lock held across Lua; handlers may submit/remove/release their layer.
        callback("on_pda_map_input", event);
        luabind::object consumed = event["consume"];
        return consumed.type() == LUA_TBOOLEAN && luabind::object_cast<bool>(consumed);
    }

    void visibility(Store& s, bool shown)
    {
        if (!shown)
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            s.input_layer = 0;
        }
        luabind::functor<void> callback;
        if (ai().script_engine().functor("_G.SendScriptCallback", callback))
            callback("on_pda_map_visibility", shown);
    }

    void reset(Store& s)
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.input_layer = 0;
        s.point_count = 0;
        s.layers.clear();
    }

    class Batch
    {
        xr_vector<DrawVertex>& vertices;
        CUIStaticItem* bound = nullptr;
    public:
        explicit Batch(xr_vector<DrawVertex>& v) : vertices(v) { vertices.clear(); vertices.reserve(8190); }
        void flush()
        {
            if (vertices.empty()) return;
            UIRender->StartPrimitive(u32(vertices.size()), IUIRender::ptTriList, UI().m_currentPointType);
            for (const DrawVertex& v : vertices)
                UIRender->PushPoint(v.vertex.pt.x, v.vertex.pt.y, 0.f, v.color, v.vertex.uv.x, v.vertex.uv.y);
            UIRender->FlushPrimitive();
            vertices.clear();
        }
        void use(CUIStaticItem* item)
        {
            if (item == bound) return;
            flush();
            UIRender->SetShader(*item->GetShader());
            bound = item;
        }
        void triangle(const S2DVert& a, const S2DVert& b, const S2DVert& c, u32 color)
        {
            if (vertices.size() + 3 > 8190) flush();
            vertices.push_back({a,color}); vertices.push_back({b,color}); vertices.push_back({c,color});
        }
    };

    // Emit a convex polygon using the same UI vertex type as the PDA pass.
    // CPU clipping also applies in the 3D PDA pass, where a screen scissor is insufficient.
    static void polygon(sPoly2D poly, C2DFrustum& clip, u32 color, Batch& batch)
    {
        for (u32 i = 0; i < poly.size(); ++i)
        {
            UI().ClientToScreenScaled(poly[i].pt);
            if (UI().m_currentPointType != IUIRender::pttLIT) poly[i].pt.sub(0.5f);
        }
        sPoly2D a, b, c;
        sPoly2D* result = clip.ClipPoly(poly, a);
        if (!result || result->size() < 3) return;
        result = UI().m_currentPointType == IUIRender::pttLIT
            ? UI().ScreenFrustumLIT().ClipPoly(*result, b)
            : UI().ActiveClipFrustum().ClipPoly(*result, b);
        if (!result || result->size() < 3) return;
        if (UI().m_currentPointType == IUIRender::pttLIT && UI().HasCustomClip())
            result = UI().ActiveClipFrustum().ClipPoly(*result, c);
        if (!result || result->size() < 3) return;
        for (u32 i = 1; i + 1 < result->size(); ++i)
            batch.triangle((*result)[0], (*result)[i], (*result)[i + 1], color);
    }

    static CUIStaticItem* texture(Layer& layer, const shared_str& id)
    {
        auto it = layer.textures.find(id);
        if (it != layer.textures.end()) return it->second.get();
        if (layer.textures.size() >= max_textures_per_layer || !texture_valid(id.c_str())) return nullptr;
        std::unique_ptr<CUIStaticItem> item(new CUIStaticItem());
        CUITextureMaster::InitTexture(id, item.get());
        auto* ptr = item.get();
        layer.textures.emplace(id, std::move(item));
        return ptr;
    }

    static void draw(Store& s)
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.layers.empty() || !s.map.GlobalMap() || !s.map.GlobalMap()->IsShown()) return;
        Frect rect = s.map.ActiveMapRect();
        if (rect.width() <= 0.f || rect.height() <= 0.f) return;
        Fvector2 lt, rb;
        UI().ClientToScreenScaled(lt, rect.x1, rect.y1);
        UI().ClientToScreenScaled(rb, rect.x2, rect.y2);
        C2DFrustum clip;
        clip.CreateFromRect(Frect().set(lt.x, lt.y, rb.x, rb.y));
        UI().PushScissor(rect);
        Batch batch(s.vertices);
        for (auto& pair : s.layers)
        {
            Layer& layer = *pair.second;
            if (!layer.visible) continue;
            for (const Command& command : layer.commands)
            {
                CUICustomMap* m = find_map(s, command.map_name.c_str());
                if (!m || !m->IsShown()) continue;
                const bool marker = command.texture.size() != 0;
                const shared_str id = marker ? command.texture : shared_str("ui_inGame2_white_rect");
                CUIStaticItem* item = texture(layer, id);
                if (!item) continue;
                batch.use(item);
                Fvector2 ts;
                UIRender->GetActiveTextureResolution(ts);
                if (ts.x <= 0.f || ts.y <= 0.f) continue;
                Frect uv = item->GetTextureRect();
                if (uv.width() <= 0.f || uv.height() <= 0.f) uv.set(0.f, 0.f, ts.x, ts.y);
                uv.x1 /= ts.x; uv.x2 /= ts.x; uv.y1 /= ts.y; uv.y2 /= ts.y;
                sPoly2D quad;
                quad.resize(4);
                if (marker)
                {
                    Fvector2 pos;
                    if (!project(s, command.map_name.c_str(), command.points[0], pos)) continue;
                    float w = command.size.x, h = command.size.y;
                    if (command.scale)
                    {
                        const float zoom = m->GetCurrentZoom().x;
                        w *= zoom * UI().get_current_kx(); h *= zoom;
                    }
                    quad[0].set(pos.x - w * .5f, pos.y - h * .5f, uv.x1, uv.y1);
                    quad[1].set(pos.x + w * .5f, pos.y - h * .5f, uv.x2, uv.y1);
                    quad[2].set(pos.x + w * .5f, pos.y + h * .5f, uv.x2, uv.y2);
                    quad[3].set(pos.x - w * .5f, pos.y + h * .5f, uv.x1, uv.y2);
                    polygon(quad, clip, command.color, batch);
                    continue;
                }
                const float u = (uv.x1 + uv.x2) * .5f, v = (uv.y1 + uv.y2) * .5f;
                const u32 n = u32(command.points.size());
                const u32 segments = command.closed ? n : n - 1;
                for (u32 i = 0; i < segments; ++i)
                {
                    Fvector2 a, b;
                    if (!project(s, command.map_name.c_str(), command.points[i], a) ||
                        !project(s, command.map_name.c_str(), command.points[(i + 1) % n], b)) continue;
                    std::array<R1PdaMapMath::Point, 4> vertices;
                    if (!R1PdaMapMath::line_quad({a.x,a.y}, {b.x,b.y}, command.width, vertices)) continue;
                    for (u32 k = 0; k < 4; ++k) quad[k].set(vertices[k].x, vertices[k].y, u, v);
                    polygon(quad, clip, command.color, batch);
                }
            }
        }
        batch.flush();
        UI().PopScissor();
    }

    class Canvas : public CUIWindow
    {
        Store& store;
    public:
        explicit Canvas(Store* s) : store(*s) { SetAutoDelete(true); Enable(false); }
        virtual void Draw() override { draw(store); }
    };

    Store* create(CUIMapWnd& map) { return new Store(map); }
    void destroy(Store*& s) { delete s; s = nullptr; }
    CUIWindow* create_canvas(Store& s) { return xr_new<Canvas>(&s); }
    static u32 version() { return 1; }
    static bool ready() { return current() != nullptr; }

    void script_register(lua_State* L)
    {
        using namespace luabind;
        module(L, "pda_map")
        [
            def("version", &version), def("ready", &ready),
            def("register_layer", &register_layer), def("valid_layer", &valid_layer),
            def("release_layer", &release_layer), def("clear_layer", &clear_layer),
            def("set_visible", &set_visible), def("count", &count),
            def("acquire_input", &acquire_input), def("release_input", &release_input),
            def("add_line", &add_line), def("add_polyline", &add_polyline),
            def("add_marker", &add_marker), def("remove", &remove),
            def("to_screen", &to_screen, pure_out_value<4>() + pure_out_value<5>()),
            def("from_screen", &from_screen, pure_out_value<4>() + pure_out_value<5>()),
            def("viewport", &viewport, raw<1>())
        ];
    }
}
