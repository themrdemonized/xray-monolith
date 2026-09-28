#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <ppl.h>
#include <DirectXMath.h>
#include <array>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include "MinHook.h"
#include "R1_hosts.h"
#include "R1_stereo_math.h"
#include "R1_gpu.h"
#include "R1_pair_state.h"
#include "R1_decompression.h"
using namespace DirectX;
#define R1_API extern "C" __declspec(dllexport)
struct R1Status {unsigned size,installed,enabled,pairs,passes,frame,restored;int error;};
namespace {
using Method=void(__fastcall*)(void*);
using Cache=void(__fastcall*)(void*,void*,void*);
unsigned char* base=nullptr;const R1Host* host=nullptr;DWORD owner=0;
Method renderOriginal=nullptr,endOriginal=nullptr,homOriginal=nullptr;
Method uiOriginal=nullptr;
Method cursorOriginal=nullptr;
Method consoleOriginal=nullptr;
using DecompressMethod=void(__fastcall*)(void*,void*);
DecompressMethod decompressOriginal=nullptr;
const R1Function decompressHook={0xaf7000,{0x40,0x57,0x48,0x83,0xec,0x30,0xf3,0x0f,0x10,0x05,0xae,0xa4,0xad,0x00,0x48,0x8b,0xfa,0xf3,0x0f,0x59,0x05,0xdf,0xfd,0x6d}};
const R1Function consoleHook={0x92460,{0x48,0x8b,0xc4,0x56,0x48,0x81,0xec,0xf0,0x00,0x00,0x00,0x80,0xb9,0x5c,0x81,0x00,0x00,0x00,0x48,0x8b,0xf1,0x0f,0x84,0xa5}};
const R1Function cursorHook={0x19f7d0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x3d,0x5f,0xf9,0x42,0x01,0x48,0x8b,0xd9,0x80,0xbf,0x88,0x01}};
bool drawing_ui=false;
const R1Function uiHook={0x1fdca0,{0x40,0x55,0x48,0x83,0xec,0x70,0xf7,0x05,0x2c,0x02,0x24,0x01,0x00,0x10,0x00,0x00,0x48,0x8b,0xe9,0x0f,0x84,0xf4,0x03,0x00}};
const R1Function detailCalc={0xb6c050,{0x40,0x53,0x48,0x83,0xec,0x50,0x48,0x83,0x3d,0xca,0x79,0xa6,0x00,0x00,0x48,0x8b,0xd9,0x0f,0x84,0x0d,0x02,0x00,0x00,0x48}};
using FontMethod=void(__fastcall*)(void*,void*);
FontMethod fontOriginal=nullptr;
const R1Function fontHook={0xb1f490,{0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x8d,0xac,0x24,0xe0,0xdf,0xff,0xff,0xb8,0x20,0x21}};
R1GPU gpu;R1Status status{sizeof(R1Status)};bool in_pair=false,have_pair=false;
bool menu_sbs=false,menu_reported=false;
float ipd=.064f,convergence=2.f;ULONGLONG heartbeat=0;std::ofstream logFile;std::string capture_path;
bool capture_loading_only=false;
template<class T>T& field(void* p,unsigned offset){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
template<class T>T& global(unsigned rva){return field<T>(base,rva);}
void* device(){return base+host->device;}
void note(const char* text){if(logFile){logFile<<GetTickCount64()<<" "<<text<<"\n";logFile.flush();}}
void cache(){auto d=device();auto object=field<void*>(d,2064);reinterpret_cast<Cache>(base+host->cache)(object,static_cast<unsigned char*>(d)+112,static_cast<unsigned char*>(d)+304);reinterpret_cast<Cache>(base+host->cache_prev)(object,static_cast<unsigned char*>(d)+688,static_cast<unsigned char*>(d)+880);}
bool identify(){
    wchar_t path[32768];auto length=GetModuleFileNameW(nullptr,path,32768);if(!length||length>=32768)return false;
    std::ifstream file(path,std::ios::binary);if(!file)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char digest[32]{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    bool ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    std::array<char,65536> buffer{};
    while(ok&&file){file.read(buffer.data(),buffer.size());auto n=file.gcount();if(n)ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),ULONG(n),0)>=0;}
    ok=ok&&!file.bad()&&BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);if(!ok)return false;
    char hex[65];for(unsigned i=0;i<32;++i)sprintf_s(hex+i*2,3,"%02x",digest[i]);
    for(const auto& h:R1_HOSTS)if(strcmp(h.hash,hex)==0)host=&h;
    if(!host||strcmp(host->name,"DX11"))return false;base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>4096)return false;
    auto pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||pe->FileHeader.TimeDateStamp!=host->timestamp||pe->OptionalHeader.SizeOfImage!=host->image_size)return false;
    for(const auto& f:host->hooks)if(f.rva>host->image_size-24||memcmp(base+f.rva,f.bytes,24))return false;
    if(memcmp(base+fontHook.rva,fontHook.bytes,24)||memcmp(base+decompressHook.rva,decompressHook.bytes,24))return false;
    if(memcmp(base+uiHook.rva,uiHook.bytes,24)||memcmp(base+detailCalc.rva,detailCalc.bytes,24))return false;
    if(memcmp(base+cursorHook.rva,cursorHook.bytes,24))return false;
    if(memcmp(base+consoleHook.rva,consoleHook.bytes,24))return false;
    return true;
}
bool targets(ComPtr<ID3D11Texture2D>& back,ID3D11RenderTargetView*& rt,ID3D11DeviceContext*& context){
    auto d=global<ID3D11Device*>(host->hw+48);context=global<ID3D11DeviceContext*>(host->hw+56);rt=global<ID3D11RenderTargetView*>(host->hw+72);
    if(!d||!context||!rt)return false;ComPtr<ID3D11Resource> resource;rt->GetResource(&resource);
    return SUCCEEDED(resource.As(&back))&&gpu.setup(d,back.Get());
}
bool world(){
    if(!status.enabled||GetTickCount64()-heartbeat>1500||GetCurrentThreadId()!=owner)return false;
    auto d=device();auto level=global<void*>(host->level);auto persistent=global<void*>(host->persistent);
    if(!level||!persistent||!field<unsigned char>(level,524912)||field<unsigned>(d,24))return false;
    auto menu=field<void*>(persistent,2608);if(menu&&reinterpret_cast<bool(__fastcall*)(void*)>(base+host->menu)(menu))return false;
    if(global<int>(host->hdr)||global<int>(host->msaa)||field<unsigned char>(d,2576)){status.error=-20;return false;}
    // PDB ps_ssfx_taa, September DX11 only. Separate eye histories are not implemented.
    if(global<float>(0x143fe78)>0){status.error=-25;return false;}
    return true;
}
struct Snapshot {
    std::array<unsigned char,1200> camera;
    std::array<unsigned char,256> inverse;
    float aspect;
    Snapshot(){auto d=device();memcpy(camera.data(),static_cast<unsigned char*>(d)+64,camera.size());memcpy(inverse.data(),static_cast<unsigned char*>(d)+2320,inverse.size());aspect=field<float>(d,1504);}
    XMMATRIX matrix(unsigned offset)const{return XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(camera.data()+offset-64));}
    void restore(){auto d=device();memcpy(static_cast<unsigned char*>(d)+64,camera.data(),camera.size());memcpy(static_cast<unsigned char*>(d)+2320,inverse.data(),inverse.size());field<float>(d,1504)=aspect;cache();}
    ~Snapshot(){restore();}
};
void store(unsigned offset,FXMMATRIX m){XMStoreFloat4x4(&field<XMFLOAT4X4>(device(),offset),m);}
void eye(const Snapshot& snapshot,float offset){
    auto d=device();snapshot.matrix(112);
    auto original=*reinterpret_cast<const r1st::V*>(snapshot.camera.data());
    auto right=*reinterpret_cast<const r1st::V*>(snapshot.camera.data()+36);
    field<r1st::V>(d,64)=r1st::add(original,right,offset);
    // Each full-size temporary target represents one 16:9 eye, then downsamples horizontally into SBS.
    // X-Ray stores height / width, unlike most graphics APIs.
    field<float>(d,1504)=snapshot.aspect*2.f;
    for(unsigned mode=0;mode<3;++mode){
        unsigned viewOff=112+64*mode,projOff=304+64*mode,fullOff=496+64*mode;
        auto view=XMMatrixMultiply(snapshot.matrix(viewOff),XMMatrixTranslation(-offset,0,0));
        XMFLOAT4X4 projection;XMStoreFloat4x4(&projection,snapshot.matrix(projOff));projection._11*=2.f;
        projection._31+=offset*projection._11/convergence;
        auto proj=XMLoadFloat4x4(&projection),full=XMMatrixMultiply(view,proj);
        store(viewOff,view);store(projOff,proj);store(fullOff,full);
        // Temporal effects are unsupported in this first probe: use zero camera motion per eye.
        store(viewOff+576,view);store(projOff+576,proj);store(fullOff+576,full);
        if(mode==0){store(2320,XMMatrixInverse(nullptr,view));store(2384,XMMatrixInverse(nullptr,proj));store(2512,XMMatrixInverse(nullptr,full));}
        if(mode==1)store(2448,XMMatrixInverse(nullptr,proj));
    }
    cache();
}
void __fastcall onDecompress(void* self,void* constant){
    decompressOriginal(self,constant);
    if(!in_pair||!constant)return;
    const auto& p=field<XMFLOAT4X4>(device(),304);
    if(p._31==0.f&&p._32==0.f)return;
    // September PDB: CBackend buffer arrays, R_constant loads, dx10ConstantBuffer.
    const unsigned masks[]={1,2,8,16,32,64},shifts[]={16,12,8,20,24,28};
    const unsigned arrays[]={1256,1144,1368,1480,1592,1704};
    const auto destination=field<unsigned>(constant,28);
    for(unsigned i=0;i<6;++i)if(destination&masks[i]){
        auto index=(destination>>shifts[i])&15;
        if(index>=14){status.error=-27;return;}
        auto buffer=global<void*>(0x15d53d0+arrays[i]+index*8);
        auto offset=field<unsigned short>(constant,32+i*4);
        if(!buffer||offset+16u>field<unsigned>(buffer,112)||!field<void*>(buffer,120)){
            status.error=-27;return;
        }
        auto value=reinterpret_cast<float*>(static_cast<unsigned char*>(field<void*>(buffer,120))+offset);
        r1st::correct_decompression(value,p._11,p._22,p._31,p._32);
        field<bool>(buffer,128)=true;
    }
}
void __fastcall onHom(void* self){
    // Rebuild the occlusion raster for the actual eye instead of globally disabling culling.
    // Visibility-delay caches fail open in this source: the second eye may draw extra,
    // but must not reuse a hidden result from the first eye. Workers were joined before the pair.
    if(in_pair)field<unsigned>(self,188)=field<unsigned>(device(),40)-1;
    homOriginal(self);
}
bool loadingActive(){
    auto app=global<void*>(0x15b5a58);
    return app&&field<unsigned>(app,6176)!=0&&!global<int>(0x15b463c);
}
bool menuActive(){
    if(!menu_sbs||GetCurrentThreadId()!=owner)return false;
    // CApplication::LoadDraw bypasses the main-menu render path but still calls Device.End.
    // Exact September PDB: BOOL g_appLoaded. The loading card needs identical images per eye.
    if(loadingActive())return true;
    auto persistent=global<void*>(host->persistent);
    auto menu=persistent?field<void*>(persistent,2608):nullptr;
    return menu&&reinterpret_cast<bool(__fastcall*)(void*)>(base+host->menu)(menu);
}
void __fastcall onFont(void* self,void* font){
    if((!menuActive()&&!drawing_ui)||!gpu.height||gpu.width<gpu.height*3||gpu.width>gpu.height*4){fontOriginal(self,font);return;}
    // UI rectangles scale with screen width, but glyph widths do not. SBS halves need twice-wide glyphs before packing.
    auto& scale=global<float>(0x143df00);const float saved=scale;
    struct Restore{float& value;float saved;~Restore(){value=saved;}}restore{scale,saved};
    scale*=2; fontOriginal(self,font);
}
struct PairGuard {
    void* hom;int enabled;
    PairGuard():hom(base+host->render+528),enabled(field<int>(hom,56)){in_pair=true;}
    ~PairGuard(){field<int>(hom,56)=enabled;in_pair=false;}
};
struct RenderFlagGuard {
    bool value;
    RenderFlagGuard():value(field<bool>(device(),2264)){field<bool>(device(),2264)=false;}
    ~RenderFlagGuard(){field<bool>(device(),2264)=value;}
};
void __fastcall onRender(void* self){
    have_pair=false;
    if(in_pair||!world()){renderOriginal(self);return;}
    ComPtr<ID3D11Texture2D> back;ID3D11RenderTargetView* rt=nullptr;ID3D11DeviceContext* context=nullptr;
    if(!targets(back,rt,context)){status.error=-21;renderOriginal(self);return;}
    if(gpu.width<gpu.height*3||gpu.width>gpu.height*4){status.error=-24;renderOriginal(self);return;}
    const auto pair_start=std::chrono::steady_clock::now();
    // Finish the existing frame jobs before touching camera matrices. This does not rerun simulation.
    static_assert(sizeof(Concurrency::task_group)==232,"MT task_group ABI");
    auto d=device();{RenderFlagGuard flag;field<Concurrency::task_group>(d,2584).wait();}
    auto details=global<void*>(host->render+1112);
    if(details)reinterpret_cast<Method>(base+detailCalc.rva)(details);
    r1st::DetailLists detailLists;r1st::LightFrames lightFrames;
    if(!detailLists.capture(details)||!lightFrames.capture(base+host->render+7608)){
        status.error=-26;renderOriginal(self);return;
    }
    Snapshot snapshot;const unsigned frame=field<unsigned>(d,40);
    auto renderTarget=global<void*>(host->render+1136);
    if(!renderTarget){status.error=-21;renderOriginal(self);return;}
    {PairGuard pair;
    // phase_accumulator otherwise clears only once per Device.dwFrame, adding right-eye
    // light over the completed left-eye buffer. Invalidate only this render marker.
    field<unsigned>(renderTarget,16)=frame-1;
    eye(snapshot,-ipd);renderOriginal(self);++status.passes;context->CopyResource(gpu.left.Get(),back.Get());
    if(!detailLists.restore()){status.error=-26;status.enabled=0;return;}
    lightFrames.restore();
    field<unsigned>(renderTarget,16)=frame-1;
    eye(snapshot,0);renderOriginal(self);++status.passes;context->CopyResource(gpu.right.Get(),back.Get());
    }snapshot.restore();
    if(frame!=field<unsigned>(d,40)){status.error=-22;status.enabled=0;note("ERROR simulation frame advanced between eyes");return;}
    status.frame=frame;status.restored++;status.error=0;have_pair=true;
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pair_start).count();
    static unsigned samples=0;static double total=0,peak=0;
    total+=ms;peak=(std::max)(peak,ms);
    if(++samples==120){char line[256];sprintf_s(line,"PAIR_CPU_SUBMIT_WAIT_MS avg=%.3f max=%.3f samples=%u lights=%zu target=%ux%u (not GPU timing)",total/samples,peak,samples,lightFrames.entries.size(),gpu.width,gpu.height);note(line);samples=0;total=peak=0;}
}
void overlay(void* self,Method original,bool cursor){
    if(!have_pair||!world()||drawing_ui){original(self);return;}
    ComPtr<ID3D11Texture2D> back;ID3D11RenderTargetView* rt=nullptr;ID3D11DeviceContext* context=nullptr;
    if(!targets(back,rt,context)){status.error=-21;have_pair=false;original(self);return;}
    struct Guard{Guard(){drawing_ui=true;}~Guard(){drawing_ui=false;}}guard;
    const unsigned stamp=global<unsigned>(0x15c02cc);
    context->CopyResource(back.Get(),gpu.left.Get());original(self);context->CopyResource(gpu.left.Get(),back.Get());
    if(cursor)global<unsigned>(0x15c02cc)=stamp;
    context->CopyResource(back.Get(),gpu.right.Get());original(self);context->CopyResource(gpu.right.Get(),back.Get());
}
void __fastcall onUI(void* self){overlay(self,uiOriginal,false);}
void __fastcall onCursor(void* self){overlay(self,cursorOriginal,true);}
void __fastcall onConsole(void* self){overlay(self,consoleOriginal,false);}
void __fastcall onEnd(void* self){
    bool menu_frame=menuActive();
    if((have_pair&&world())||menu_frame){
        ComPtr<ID3D11Texture2D> back;ID3D11RenderTargetView* rt=nullptr;ID3D11DeviceContext* context=nullptr;
        if(targets(back,rt,context)){
            if(menu_frame){
                if(gpu.width<gpu.height*3||gpu.width>gpu.height*4){endOriginal(self);return;}
                context->CopyResource(gpu.left.Get(),back.Get());context->CopyResource(gpu.right.Get(),back.Get());
            }
            // World eye textures include CHUDManager::RenderUI; menus use the full backbuffer.
            if(gpu.draw(context,rt)){
                if(!menu_frame){++status.pairs;if(status.pairs==1)note("FIRST_PAIR 0.1.4 candidate; UI hook; detail and light replay; visual acceptance pending");}
                else if(!menu_reported){menu_reported=true;note("MENU_SBS identical full menu per eye");}
                if(!capture_path.empty()&&(!capture_loading_only||loadingActive())){
                    D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);desc.BindFlags=0;desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                    ComPtr<ID3D11Device> d;back->GetDevice(&d);ComPtr<ID3D11Texture2D> copy;
                    if((desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)&&SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&copy))){
                        context->CopyResource(copy.Get(),back.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
                        if(SUCCEEDED(context->Map(copy.Get(),0,D3D11_MAP_READ,0,&mapped))){
                            std::ofstream f(capture_path,std::ios::binary);f<<"P6\n"<<desc.Width<<" "<<desc.Height<<"\n255\n";
                            for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x)f.write(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch+x*4,3);
                            context->Unmap(copy.Get(),0);note(f?"CAPTURE_WRITTEN":"CAPTURE_WRITE_FAILED");
                        }
                    }
                    capture_path.clear();
                }
            }
            else status.error=-23;
        }
    }
    have_pair=false;endOriginal(self);
}
}
R1_API int r1st_install(const char* log_path){
    if(status.installed)return 1;if(log_path)logFile.open(log_path,std::ios::app);
    if(!identify()){status.error=-10;note("REFUSED unknown host or modified hook site");return -10;}
    owner=GetCurrentThreadId();if(MH_Initialize()!=MH_OK)return -11;
    void* hooks[]={reinterpret_cast<void*>(&onRender),reinterpret_cast<void*>(&onEnd),reinterpret_cast<void*>(&onHom),reinterpret_cast<void*>(&onFont),reinterpret_cast<void*>(&onUI),reinterpret_cast<void*>(&onCursor),reinterpret_cast<void*>(&onConsole),reinterpret_cast<void*>(&onDecompress)};
    void** originals[]={reinterpret_cast<void**>(&renderOriginal),reinterpret_cast<void**>(&endOriginal),reinterpret_cast<void**>(&homOriginal),reinterpret_cast<void**>(&fontOriginal),reinterpret_cast<void**>(&uiOriginal),reinterpret_cast<void**>(&cursorOriginal),reinterpret_cast<void**>(&consoleOriginal),reinterpret_cast<void**>(&decompressOriginal)};
    const unsigned rvas[]={host->hooks[0].rva,host->hooks[1].rva,host->hooks[2].rva,fontHook.rva,uiHook.rva,cursorHook.rva,consoleHook.rva,decompressHook.rva};
    for(unsigned i=0;i<8;++i)if(MH_CreateHook(base+rvas[i],hooks[i],originals[i])!=MH_OK){MH_Uninitialize();return -12;}
    HMODULE pin;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&r1st_install),&pin)){MH_Uninitialize();return -13;}
    for(unsigned i=0;i<8;++i)if(MH_EnableHook(base+rvas[i])!=MH_OK){for(unsigned j=0;j<i;++j)MH_DisableHook(base+rvas[j]);MH_Uninitialize();return -14;}
    status.installed=1;note("INSTALLED default-off exact September MT host");return 1;
}
R1_API int r1st_set(unsigned enabled,float eye_distance,float focus){
    if(!status.installed||GetCurrentThreadId()!=owner||in_pair)return -2;
    if(enabled>1||!std::isfinite(eye_distance)||eye_distance<0||eye_distance>.085f||!std::isfinite(focus)||focus<.5f||focus>10000)return -3;
    if(enabled&&!status.enabled)status.error=0;
    status.enabled=enabled;ipd=eye_distance;convergence=focus;heartbeat=GetTickCount64();return 1;
}
R1_API int r1st_status(R1Status* out){if(!out||out->size!=sizeof(R1Status)||GetCurrentThreadId()!=owner)return -3;*out=status;return 1;}
R1_API int r1st_capture(const char* path){if(!status.installed||GetCurrentThreadId()!=owner||!path||strlen(path)>2000)return -3;capture_path=path;capture_loading_only=false;return 1;}
R1_API int r1st_capture_loading(const char* path){int result=r1st_capture(path);if(result==1)capture_loading_only=true;return result;}
R1_API int r1st_menu(unsigned enabled){if(!status.installed||GetCurrentThreadId()!=owner||enabled>1)return -3;menu_sbs=enabled!=0;return 1;}
R1_API int r1st_shutdown(){
    if(!status.installed)return 0;if(GetCurrentThreadId()!=owner||in_pair)return -2;
    status.enabled=0;have_pair=false;for(const auto& f:host->hooks)MH_DisableHook(base+f.rva);
    MH_DisableHook(base+fontHook.rva);menu_sbs=false;
    MH_DisableHook(base+uiHook.rva);
    MH_DisableHook(base+cursorHook.rva);
    MH_DisableHook(base+consoleHook.rva);
    MH_Uninitialize();gpu=R1GPU{};status.installed=0;note("SHUTDOWN");return 1;
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}
