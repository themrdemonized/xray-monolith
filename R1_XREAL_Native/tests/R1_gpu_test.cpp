#include "../native/R1_gpu.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
void check(bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
int main(){
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
 check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)),"WARP device");
 D3D11_TEXTURE2D_DESC desc{};desc.Width=128;desc.Height=32;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> output;ComPtr<ID3D11RenderTargetView> view;
 check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&output))&&SUCCEEDED(d->CreateRenderTargetView(output.Get(),nullptr,&view)),"output");
 R1GPU gpu;check(gpu.setup(d.Get(),output.Get()),"GPU setup");
 gpu.ps.Reset();gpu.right_view.Reset();
 check(gpu.setup(d.Get(),output.Get())&&gpu.ps&&gpu.right_view,"recover incomplete GPU initialization");
 std::vector<unsigned> red(128*32,0xff0000ffu),blue(128*32,0xffff0000u);
 c->UpdateSubresource(gpu.left.Get(),0,nullptr,red.data(),128*4,0);c->UpdateSubresource(gpu.right.Get(),0,nullptr,blue.data(),128*4,0);
 D3D11_VIEWPORT original{7,9,33,17,0,1};c->RSSetViewports(1,&original);
 check(gpu.draw(c.Get(),view.Get()),"SBS draw");
 UINT count=1;D3D11_VIEWPORT after{};c->RSGetViewports(&count,&after);
 check(after.TopLeftX==7&&after.Width==33&&after.TopLeftY==9&&after.Height==17,"restore caller viewport");
 desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;
 check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&read)),"readback texture");c->CopyResource(read.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
 check(SUCCEEDED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)),"readback");
 for(unsigned y=0;y<32;y++)for(unsigned x=0;x<128;x++){
  unsigned pixel=*reinterpret_cast<const unsigned*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch+x*4);
  check(pixel==(x<64?0xff0000ffu:0xffff0000u),"eye order, edge coverage and full-size composition");
 }
 c->Unmap(read.Get(),0);
 std::puts("PASS WARP SBS 4096 pixels, eye order, edge coverage, caller viewport restoration");
}
