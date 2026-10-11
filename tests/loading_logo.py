"""Exercise production loading-logo methods with a fake renderer/filesystem, no game."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
application = (root / 'src/xrEngine/x_ray.cpp').read_text()
renderer = (root / 'src/Layers/xrRender/dxApplicationRender.cpp').read_text()


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


selection = function(application, 'void CApplication::LoadScreenSetLevelLogo(')
begin = function(application, 'void CApplication::LoadBegin(')
activation = function(application, 'void CApplication::Level_Set(')
end = function(application, 'void CApplication::LoadEnd(')
assert 'Level_Set(' not in selection and 'Level_ID(' not in selection
assert 'get_path(' not in selection and 'LoadDraw(' not in selection
assert 'm_loading_logo[0] = 0;' in end and 'm_loading_logo_level[0] = 0;' in end
assert begin.index('m_pRender->LoadBegin();') < begin.index('setLevelLogo(m_loading_logo)')
transition = function((root / 'src/xrGame/alife_update_manager.cpp').read_text(),
                      'bool CALifeUpdateManager::change_level(')
assert transition.index('LoadScreenSetLevelLogo(') < transition.index('Level().ClientSave();')
print('Loading-logo call order and absence of early active-zone mutation: passed')
if '--source-only' in sys.argv:
    raise SystemExit(0)

prefix = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>
#define IC inline
#define ICN inline
#define XRCORE_API
#define PROF_EVENT()
#define FALSE false
#define TRUE true
#define stricmp _stricmp
#define itoa _itoa
using s32 = int;
using u32 = unsigned;
using LPCSTR = const char*;
using string_path = char[260];
using string16 = char[16];
inline size_t xr_strlen(LPCSTR text) { return std::strlen(text); }
template<size_t N> void xr_strcpy(char (&dest)[N], LPCSTR src) { strcpy_s(dest, N, src); }
template<size_t N> void xr_strcat(char (&dest)[N], LPCSTR src) { strcat_s(dest, N, src); }
void xr_strcat(char* dest, size_t size, LPCSTR src) { strcat_s(dest, size, src); }
void strconcat(size_t size, char* dest, LPCSTR a, LPCSTR b) {
    strcpy_s(dest, size, a); strcat_s(dest, size, b);
}
bool g_dedicated_server = false, g_appLoaded = true, g_bootComplete = true;
struct Timer { void Start() {} int GetElapsed_ms() { return 0; } } phase_timer;
struct { long long mem_usage() { return 0; } } Memory;
struct FakeConsole { void Execute(LPCSTR) {} } console;
FakeConsole* Console = &console;
template<typename... Args> void Msg(LPCSTR, Args...) {}
void _InitializeFont(int&, LPCSTR, int) {}
struct Shader {
    std::string name;
    void create(LPCSTR, LPCSTR texture) { name = texture; }
    void destroy() { name.clear(); }
};
struct dxApplicationRender {
    Shader hLevelLogo;
    void setLevelLogo(LPCSTR);
    void LoadBegin() { hLevelLogo.destroy(); }
};
struct Path { std::string folder = "old-zone\\"; void _set(LPCSTR value) { folder = value; } };
struct FileSystem {
    std::set<std::string> textures, local;
    Path level;
    int probes = 0;
    Path* get_path(LPCSTR name) { assert(std::string(name) == "$level$"); return &level; }
    bool exist(string_path& out, LPCSTR alias, LPCSTR name, LPCSTR extension) {
        ++probes; strcpy_s(out, name); strcat_s(out, extension);
        if (std::string(alias) == "$game_textures$") return textures.count(out) != 0;
        assert(std::string(alias) == "$level$");
        return local.count(level.folder + out) != 0;
    }
} FS;
struct CApplication {
    dxApplicationRender render;
    dxApplicationRender* m_pRender = &render;
    string_path m_loading_logo_level{}, m_loading_logo{};
    u32 ll_dwReference = 0, Level_Current = u32(-1);
    int pFontSystem = 0, load_stage = 0;
    struct Level { LPCSTR folder; };
    std::vector<Level> Levels{{"l06_rostok\\"}, {"l07_military\\"}, {"no-artwork\\"}, {"local-zone\\"}};
    void LoadScreenSetLevelLogo(LPCSTR);
    void LoadBegin();
    void LoadEnd();
    void Level_Set(u32);
};
'''
checks = r'''
int main() {
    FS.textures = {"intro\\intro_l06_rostok_1.dds", "intro\\intro_l06_rostok_2.dds",
                   "intro\\intro_l07_military_1.dds", "intro\\intro_l07_military_2.dds"};
    for (int seed = 1; seed <= 128; ++seed) {
        CApplication app;
        Random.seed(seed);
        CRandom expected = Random;
        app.LoadScreenSetLevelLogo("l07_military");
        assert(FS.level.folder == "old-zone\\" && app.Level_Current == u32(-1));
        assert(app.render.hLevelLogo.name.empty()); // No render-resource changes while playing.
        CRandom observed = Random;
        assert(observed.randI() == expected.randI());
        std::string chosen = app.m_loading_logo;
        int probes = FS.probes;
        app.LoadScreenSetLevelLogo("L07_MILITARY\\");
        assert(FS.probes == probes && chosen == app.m_loading_logo);
        app.LoadBegin();
        assert(app.render.hLevelLogo.name == chosen); // First loading frame has destination artwork.
        app.LoadBegin(); // Nested initialization does not clear or reroll it.
        assert(app.render.hLevelLogo.name == chosen);
        expected = Random;
        for (int i = 0; i < 10; ++i) { Random.randI(); expected.randI(); }
        app.Level_Set(1);
        expected.randI(2); // Exactly the legacy selection draw, at activation.
        observed = Random;
        assert(observed.randI() == expected.randI());
        assert(app.render.hLevelLogo.name == chosen && FS.level.folder == "l07_military\\");
        expected = Random;
        app.Level_Set(1);
        observed = Random;
        assert(observed.randI() == expected.randI()); // Repeated activation does not consume RNG.
        app.Level_Set(99);
        assert(FS.level.folder == "l07_military\\");
        app.LoadEnd();
        assert(!std::string(app.m_loading_logo).empty()); // Nested load retains the preview.
        app.LoadEnd();
        assert(!app.m_loading_logo[0] && !app.m_loading_logo_level[0]);
        assert(app.render.hLevelLogo.name == chosen); // Precache still uses the bound image.
        app.LoadBegin();
        assert(app.render.hLevelLogo.name.empty()); // A later generic load cannot reuse old artwork.
        app.LoadEnd();
        FS.level.folder = "old-zone\\";
    }
    CApplication app;
    app.LoadScreenSetLevelLogo(nullptr);
    app.LoadScreenSetLevelLogo("");
    assert(!app.m_loading_logo[0]);
    app.LoadScreenSetLevelLogo("l06_rostok");
    app.LoadBegin();
    assert(!app.render.hLevelLogo.name.empty());
    app.LoadScreenSetLevelLogo("no-artwork");
    assert(app.render.hLevelLogo.name.empty()); // Never show a previous zone for missing artwork.
    app.Level_Set(2);
    assert(app.render.hLevelLogo.name.empty());
    FS.local.insert("local-zone\\intro\\intro_local-zone_1.dds");
    app.LoadScreenSetLevelLogo("local-zone");
    assert(app.render.hLevelLogo.name.empty()); // Local-only textures cannot resolve before activation.
    app.Level_Set(3);
    assert(app.render.hLevelLogo.name == "intro\\intro_local-zone_1");
    CApplication generic;
    generic.render.hLevelLogo.name = "stale";
    generic.LoadBegin();
    assert(generic.render.hLevelLogo.name.empty());
    generic.render.setLevelLogo(nullptr);
    assert(generic.render.hLevelLogo.name.empty());
    g_dedicated_server = true;
    generic.render.hLevelLogo.name = "untouched";
    generic.LoadScreenSetLevelLogo("l06_rostok");
    assert(generic.render.hLevelLogo.name == "untouched");
    std::puts("Production loading-logo methods: 128 RNG seeds, first-frame artwork, no reroll, fallback and active-zone preservation passed");
}
'''
out = root / '_build'
out.mkdir(exist_ok=True)
source = out / 'loading_logo.cpp'
random = (root / 'src/xrCore/_random.h').read_text()
source.write_text(prefix + random + '\nCRandom Random;\n' +
                  function(renderer, 'void dxApplicationRender::setLevelLogo(') +
                  function(application, 'void gen_logo_name(') + selection + begin + end + activation + checks)
exe = out / 'loading_logo.exe'
subprocess.run(['cl', '/nologo', '/std:c++17', '/EHsc', '/O2', '/MD', str(source),
                '/Fe:' + str(exe), '/Fo:' + str(out / 'loading_logo.obj')], check=True)
subprocess.run([str(exe)], check=True)
