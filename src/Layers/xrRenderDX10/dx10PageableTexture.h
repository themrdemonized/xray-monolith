#pragma once

// CPU-side stand-in for a DDS texture. The D3D resource is created on first
// use from these tightly packed subresources and then this copy is freed.
// Include after the renderer's stdafx.h so the D3D typedefs exist.

struct DxPageableTexture
{
	u8* bytes;
	u32 byteCount;
	D3D_SUBRESOURCE_DATA* subs;
	u32 subCount;
	D3D_TEXTURE2D_DESC desc;
};

// wouldStage is what texture_load was about to do after r__no_ram_textures:
// true means the legacy path would create a D3D11_USAGE_STAGING resource and
// keep it until the texture is sampled.
bool dx_pageable_textures_wanted(bool wouldStage);

// On success, *out is a new image the caller frees with dx_pageable_free.
// On failure, *out is null and reason is a short static string.
bool dx_pageable_decode(const void* file, u32 fileSize, u32 mipSkip, bool expectCube, DxPageableTexture*& out, const char*& reason);
void dx_pageable_free(DxPageableTexture*& image);
HRESULT dx_pageable_upload(const DxPageableTexture* image, ID3DTexture2D** outTexture);
