#ifndef	xrD3DDefs_included
#define	xrD3DDefs_included
#pragma once

#if defined(USE_DX11) || defined(USE_DX10)

#	include "..\xrRenderDX10\DXCommonTypes.h"

#elif defined(XRRENDER_VULKAN_EXPORTS)	//	Vulkan

// Vulkan stubs for D3D types (needed for scene graph compatibility)
typedef void* ID3DVertexShader;
typedef void* ID3DPixelShader;
typedef void* ID3DGeometryShader;
typedef void* ID3DBlob;
typedef void* ID3DQuery;
typedef void* ID3DInclude;
typedef void* ID3DTexture2D;
typedef void* ID3DRenderTargetView;
typedef void* ID3DDepthStencilView;
typedef void* ID3DBaseTexture;
typedef void* ID3DVertexBuffer;
typedef void* ID3DIndexBuffer;
typedef void* ID3DTexture3D;
typedef void* ID3DState;

struct D3D_TEXTURE2D_DESC {
	u32 Width;
	u32 Height;
	u32 MipLevels;
	u32 ArraySize;
	u32 Format;
	u32 SampleCount;
	u32 SampleQuality;
	u32 Usage;
	u32 BindFlags;
	u32 CPUAccessFlags;
	u32 MiscFlags;
};

#define DX10_ONLY(expr)			do {} while (0)

#else	//	DX9

typedef IDirect3DVertexShader9 ID3DVertexShader;
typedef IDirect3DPixelShader9 ID3DPixelShader;
typedef ID3DXBuffer ID3DBlob;
typedef D3DXMACRO D3D_SHADER_MACRO;
typedef IDirect3DQuery9 ID3DQuery;
typedef D3DVIEWPORT9 D3D_VIEWPORT;
typedef ID3DXInclude ID3DInclude;
typedef IDirect3DTexture9 ID3DTexture2D;
typedef IDirect3DSurface9 ID3DRenderTargetView;
typedef IDirect3DSurface9 ID3DDepthStencilView;
typedef IDirect3DBaseTexture9 ID3DBaseTexture;
typedef D3DSURFACE_DESC D3D_TEXTURE2D_DESC;
typedef IDirect3DVertexBuffer9 ID3DVertexBuffer;
typedef IDirect3DIndexBuffer9 ID3DIndexBuffer;
typedef IDirect3DVolumeTexture9 ID3DTexture3D;
typedef IDirect3DStateBlock9 ID3DState;

#define DX10_ONLY(expr)			do {} while (0)

#endif	//	USE_DX10


#endif	//	xrD3DDefs_included
