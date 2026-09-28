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
#include "MinHook.h"
#include "R1_hosts.h"
#include "R1_stereo_math.h"
#include "R1_gpu.h"
using namespace DirectX;
#define R1_API extern "C" __declspec(dllexport)
struct R1Status {unsigned size,installed,enabled,pairs,passes,frame,restored;int error;};
namespace {
using Method=void(__fastcall*)(void*);
using Cache=void(__fastcall*)(void*,void*,void*);
unsigned char* base=nullptr;const R1Host* host=nullptr;DWORD owner=0;
Method renderOriginal=nullptr,endOriginal=nullptr,homOriginal=nullptr;
R1GPU gpu;R1Status status{sizeof(R1Status)};bool in_pair=false,have_pair=false;
float ipd=.064f,convergence=2.f;ULONGLONG heartbeat=0;std::ofstream logFile;std::string capture_path;
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
void __fastcall onHom(void* self){
    if(in_pair){field<int>(self,56)=0;return;}homOriginal(self);
}
struct PairGuard {
    void* hom;int enabled;
    PairGuard():hom(base+host->render+528),enabled(field<int>(hom,56)){in_pair=true;field<int>(hom,56)=0;}
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
    // Finish the existing frame jobs before touching camera matrices. This does not rerun simulation.
    static_assert(sizeof(Concurrency::task_group)==232,"MT task_group ABI");
    auto d=device();{RenderFlagGuard flag;field<Concurrency::task_group>(d,2584).wait();}
    Snapshot snapshot;const unsigned frame=field<unsigned>(d,40);
    {PairGuard pair;
    eye(snapshot,-ipd);renderOriginal(self);++status.passes;context->CopyResource(gpu.left.Get(),back.Get());
    eye(snapshot,0);renderOriginal(self);++status.passes;context->CopyResource(gpu.right.Get(),back.Get());
    }snapshot.restore();
    if(frame!=field<unsigned>(d,40)){status.error=-22;status.enabled=0;note("ERROR simulation frame advanced between eyes");return;}
    status.frame=frame;status.restored++;status.error=0;have_pair=true;
}
void __fastcall onEnd(void* self){
    if(have_pair&&world()){
        ComPtr<ID3D11Texture2D> back;ID3D11RenderTargetView* rt=nullptr;ID3D11DeviceContext* context=nullptr;
        if(targets(back,rt,context)){
            // Probe deliberately excludes 2D UI until the independent HUD composition path is validated.
            if(gpu.draw(context,rt)){
                ++status.pairs;if(status.pairs==1)note("FIRST_PAIR same simulation frame; UI excluded; geometry not yet visually certified");
                if(!capture_path.empty()){
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
    void* hooks[]={reinterpret_cast<void*>(&onRender),reinterpret_cast<void*>(&onEnd),reinterpret_cast<void*>(&onHom)};
    void** originals[]={reinterpret_cast<void**>(&renderOriginal),reinterpret_cast<void**>(&endOriginal),reinterpret_cast<void**>(&homOriginal)};
    for(unsigned i=0;i<3;++i)if(MH_CreateHook(base+host->hooks[i].rva,hooks[i],originals[i])!=MH_OK){MH_Uninitialize();return -12;}
    HMODULE pin;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&r1st_install),&pin)){MH_Uninitialize();return -13;}
    for(unsigned i=0;i<3;++i)if(MH_EnableHook(base+host->hooks[i].rva)!=MH_OK){for(unsigned j=0;j<i;++j)MH_DisableHook(base+host->hooks[j].rva);MH_Uninitialize();return -14;}
    status.installed=1;note("INSTALLED default-off exact September MT host");return 1;
}
R1_API int r1st_set(unsigned enabled,float eye_distance,float focus){
    if(!status.installed||GetCurrentThreadId()!=owner||in_pair)return -2;
    if(enabled>1||!std::isfinite(eye_distance)||eye_distance<0||eye_distance>.085f||!std::isfinite(focus)||focus<.5f||focus>10000)return -3;
    if(enabled&&!status.enabled)status.error=0;
    status.enabled=enabled;ipd=eye_distance;convergence=focus;heartbeat=GetTickCount64();return 1;
}
R1_API int r1st_status(R1Status* out){if(!out||out->size!=sizeof(R1Status)||GetCurrentThreadId()!=owner)return -3;*out=status;return 1;}
R1_API int r1st_capture(const char* path){if(!status.installed||GetCurrentThreadId()!=owner||!path||strlen(path)>2000)return -3;capture_path=path;return 1;}
R1_API int r1st_shutdown(){
    if(!status.installed)return 0;if(GetCurrentThreadId()!=owner||in_pair)return -2;
    status.enabled=0;have_pair=false;for(const auto& f:host->hooks)MH_DisableHook(base+f.rva);
    MH_Uninitialize();gpu=R1GPU{};status.installed=0;note("SHUTDOWN");return 1;
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}
