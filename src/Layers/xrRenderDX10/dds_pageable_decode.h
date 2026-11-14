#pragma once
//
// Decode a DDS file into tightly packed subresources suitable for
// ID3D11Device::CreateTexture2D initial data.
//
// This is intentionally a straight copy of formats D3D11 can sample as-is.
// Anything else (volumes, 24-bit RGB, unknown FourCC, typeless DXGI formats)
// is rejected so the caller can fall back to D3DX. No conversion, no resize,
// no mip generation: the bytes that land in the GPU texture are the bytes
// stored in the file, apart from dropping whole mips when mipSkip asks for
// the same top-mip reduction CRender::texture_load already requests.
//
// Host endianness is little-endian, which is the only ABI this engine ships.

#include <cstddef>
#include <cstdint>
#include <cstring>

enum DdsDecodeStatus
{
	DdsDecode_Ok = 0,
	DdsDecode_Unsupported = 1,
	DdsDecode_Corrupt = 2
};

// DXGI_FORMAT values. dx10Texture.cpp static_asserts these against the SDK.
enum DdsDxgiFormat : uint32_t
{
	DdsFmt_R32G32B32A32_FLOAT = 2,
	DdsFmt_R16G16B16A16_FLOAT = 10,
	DdsFmt_R16G16B16A16_UNORM = 11,
	DdsFmt_R32G32_FLOAT = 16,
	DdsFmt_R10G10B10A2_UNORM = 24,
	DdsFmt_R11G11B10_FLOAT = 26,
	DdsFmt_R8G8B8A8_UNORM = 28,
	DdsFmt_R8G8B8A8_UNORM_SRGB = 29,
	DdsFmt_R8G8B8A8_SNORM = 31,
	DdsFmt_R16G16_FLOAT = 34,
	DdsFmt_R32_FLOAT = 41,
	DdsFmt_R8G8_UNORM = 49,
	DdsFmt_R16_FLOAT = 54,
	DdsFmt_R16_UNORM = 56,
	DdsFmt_R8_UNORM = 61,
	DdsFmt_A8_UNORM = 65,
	DdsFmt_BC1_UNORM = 71,
	DdsFmt_BC1_UNORM_SRGB = 72,
	DdsFmt_BC2_UNORM = 74,
	DdsFmt_BC2_UNORM_SRGB = 75,
	DdsFmt_BC3_UNORM = 77,
	DdsFmt_BC3_UNORM_SRGB = 78,
	DdsFmt_BC4_UNORM = 80,
	DdsFmt_BC4_SNORM = 81,
	DdsFmt_BC5_UNORM = 83,
	DdsFmt_BC5_SNORM = 84,
	DdsFmt_B5G6R5_UNORM = 85,
	DdsFmt_B5G5R5A1_UNORM = 86,
	DdsFmt_B8G8R8A8_UNORM = 87,
	DdsFmt_B8G8R8X8_UNORM = 88,
	DdsFmt_B8G8R8A8_UNORM_SRGB = 91,
	DdsFmt_B8G8R8X8_UNORM_SRGB = 93,
	DdsFmt_BC6H_UF16 = 95,
	DdsFmt_BC6H_SF16 = 96,
	DdsFmt_BC7_UNORM = 98,
	DdsFmt_BC7_UNORM_SRGB = 99,
	DdsFmt_B4G4R4A4_UNORM = 115
};

static const uint32_t DdsMisc_TextureCube = 0x4;
static const uint32_t DdsPlan_MaxSubs = 1024;

struct DdsPlanSubresource
{
	uint32_t fileOffset;
	uint32_t packedOffset;
	uint32_t length;
	uint32_t rowPitch;
	uint32_t slicePitch;
};

struct DdsDecodePlan
{
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t mipCount;
	uint32_t arraySize;
	uint32_t miscFlags;
	uint32_t byteCount;
	uint32_t subCount;
	DdsPlanSubresource subs[DdsPlan_MaxSubs];
};

inline bool dds_read_u32(const uint8_t* data, uint32_t size, uint32_t offset, uint32_t& out)
{
	if (offset > size || size - offset < 4)
		return false;
	memcpy(&out, data + offset, 4);
	return true;
}

inline uint32_t dds_fourcc(char a, char b, char c, char d)
{
	return (uint32_t)(uint8_t)a
		| ((uint32_t)(uint8_t)b << 8)
		| ((uint32_t)(uint8_t)c << 16)
		| ((uint32_t)(uint8_t)d << 24);
}

// blockBytes is the byte size of one texel (bw==1) or one compressed block.
inline bool dds_format_metrics(uint32_t format, uint32_t& blockBytes, uint32_t& bw, uint32_t& bh)
{
	switch (format)
	{
	case DdsFmt_BC1_UNORM:
	case DdsFmt_BC1_UNORM_SRGB:
	case DdsFmt_BC4_UNORM:
	case DdsFmt_BC4_SNORM:
		blockBytes = 8;
		bw = 4;
		bh = 4;
		return true;
	case DdsFmt_BC2_UNORM:
	case DdsFmt_BC2_UNORM_SRGB:
	case DdsFmt_BC3_UNORM:
	case DdsFmt_BC3_UNORM_SRGB:
	case DdsFmt_BC5_UNORM:
	case DdsFmt_BC5_SNORM:
	case DdsFmt_BC6H_UF16:
	case DdsFmt_BC6H_SF16:
	case DdsFmt_BC7_UNORM:
	case DdsFmt_BC7_UNORM_SRGB:
		blockBytes = 16;
		bw = 4;
		bh = 4;
		return true;
	case DdsFmt_R32G32B32A32_FLOAT:
		blockBytes = 16;
		break;
	case DdsFmt_R16G16B16A16_FLOAT:
	case DdsFmt_R16G16B16A16_UNORM:
	case DdsFmt_R32G32_FLOAT:
		blockBytes = 8;
		break;
	case DdsFmt_R10G10B10A2_UNORM:
	case DdsFmt_R11G11B10_FLOAT:
	case DdsFmt_R8G8B8A8_UNORM:
	case DdsFmt_R8G8B8A8_UNORM_SRGB:
	case DdsFmt_R8G8B8A8_SNORM:
	case DdsFmt_R16G16_FLOAT:
	case DdsFmt_R32_FLOAT:
	case DdsFmt_B8G8R8A8_UNORM:
	case DdsFmt_B8G8R8X8_UNORM:
	case DdsFmt_B8G8R8A8_UNORM_SRGB:
	case DdsFmt_B8G8R8X8_UNORM_SRGB:
		blockBytes = 4;
		break;
	case DdsFmt_R8G8_UNORM:
	case DdsFmt_R16_FLOAT:
	case DdsFmt_R16_UNORM:
	case DdsFmt_B5G6R5_UNORM:
	case DdsFmt_B5G5R5A1_UNORM:
	case DdsFmt_B4G4R4A4_UNORM:
		blockBytes = 2;
		break;
	case DdsFmt_R8_UNORM:
	case DdsFmt_A8_UNORM:
		blockBytes = 1;
		break;
	default:
		return false;
	}
	bw = 1;
	bh = 1;
	return true;
}

inline bool dds_subresource_layout(uint32_t format, uint32_t width, uint32_t height, uint32_t& rowPitch, uint32_t& slicePitch)
{
	uint32_t blockBytes = 0, bw = 1, bh = 1;
	if (!dds_format_metrics(format, blockBytes, bw, bh))
		return false;
	if (width == 0 || height == 0)
		return false;

	uint32_t blocksW = width;
	uint32_t blocksH = height;
	if (bw != 1)
	{
		blocksW = (width + bw - 1) / bw;
		blocksH = (height + bh - 1) / bh;
		if (blocksW == 0)
			blocksW = 1;
		if (blocksH == 0)
			blocksH = 1;
	}

	const uint64_t row = (uint64_t)blocksW * (uint64_t)blockBytes;
	const uint64_t slice = row * (uint64_t)blocksH;
	if (row == 0 || row > 0xffffffffu || slice > 0xffffffffu)
		return false;
	rowPitch = (uint32_t)row;
	slicePitch = (uint32_t)slice;
	return true;
}

inline bool dds_legacy_rgb_format(uint32_t bitCount, uint32_t rMask, uint32_t gMask, uint32_t bMask, uint32_t aMask, uint32_t& format)
{
	if (bitCount == 32 && rMask == 0x00ff0000u && gMask == 0x0000ff00u && bMask == 0x000000ffu)
	{
		format = (aMask == 0xff000000u) ? (uint32_t)DdsFmt_B8G8R8A8_UNORM : (uint32_t)DdsFmt_B8G8R8X8_UNORM;
		return aMask == 0xff000000u || aMask == 0;
	}
	if (bitCount == 32 && rMask == 0x000000ffu && gMask == 0x0000ff00u && bMask == 0x00ff0000u && aMask == 0xff000000u)
	{
		format = DdsFmt_R8G8B8A8_UNORM;
		return true;
	}
	if (bitCount == 16 && rMask == 0xf800u && gMask == 0x07e0u && bMask == 0x001fu && aMask == 0)
	{
		format = DdsFmt_B5G6R5_UNORM;
		return true;
	}
	if (bitCount == 16 && rMask == 0x7c00u && gMask == 0x03e0u && bMask == 0x001fu && aMask == 0x8000u)
	{
		format = DdsFmt_B5G5R5A1_UNORM;
		return true;
	}
	if (bitCount == 16 && rMask == 0x0f00u && gMask == 0x00f0u && bMask == 0x000fu && aMask == 0xf000u)
	{
		format = DdsFmt_B4G4R4A4_UNORM;
		return true;
	}
	return false;
}

inline bool dds_legacy_fourcc_format(uint32_t fourcc, uint32_t& format)
{
	if (fourcc == dds_fourcc('D', 'X', 'T', '1'))
		format = DdsFmt_BC1_UNORM;
	else if (fourcc == dds_fourcc('D', 'X', 'T', '2') || fourcc == dds_fourcc('D', 'X', 'T', '3'))
		format = DdsFmt_BC2_UNORM;
	else if (fourcc == dds_fourcc('D', 'X', 'T', '4') || fourcc == dds_fourcc('D', 'X', 'T', '5'))
		format = DdsFmt_BC3_UNORM;
	else if (fourcc == dds_fourcc('A', 'T', 'I', '1') || fourcc == dds_fourcc('B', 'C', '4', 'U'))
		format = DdsFmt_BC4_UNORM;
	else if (fourcc == dds_fourcc('B', 'C', '4', 'S'))
		format = DdsFmt_BC4_SNORM;
	else if (fourcc == dds_fourcc('A', 'T', 'I', '2') || fourcc == dds_fourcc('B', 'C', '5', 'U'))
		format = DdsFmt_BC5_UNORM;
	else if (fourcc == dds_fourcc('B', 'C', '5', 'S'))
		format = DdsFmt_BC5_SNORM;
	else
		return false;
	return true;
}

inline uint32_t dds_max_mip_count(uint32_t width, uint32_t height)
{
	uint32_t count = 1;
	while ((width > 1 || height > 1) && count < 16)
	{
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
		++count;
	}
	return count;
}

// mipSkip uses the same stopping rule as CRender::texture_load's Reduce():
// drop a mip only while more than one mip remains.
inline void dds_apply_mip_skip(uint32_t fullW, uint32_t fullH, uint32_t fileMips, uint32_t mipSkip,
	uint32_t& keptW, uint32_t& keptH, uint32_t& keptMips, uint32_t& skipped)
{
	keptW = fullW;
	keptH = fullH;
	keptMips = fileMips;
	uint32_t skipLeft = mipSkip;
	while (keptMips > 1 && skipLeft > 0)
	{
		keptW = keptW > 1 ? keptW / 2 : 1;
		keptH = keptH > 1 ? keptH / 2 : 1;
		--keptMips;
		--skipLeft;
	}
	skipped = fileMips - keptMips;
}

inline DdsDecodeStatus dds_plan_pageable(const void* fileBytes, uint32_t fileSize, uint32_t mipSkip, bool expectCube,
	DdsDecodePlan& plan, const char*& reason)
{
	reason = "unsupported dds";
	memset(&plan, 0, sizeof(plan));
	if (!fileBytes)
	{
		reason = "null dds";
		return DdsDecode_Corrupt;
	}
	const uint8_t* data = (const uint8_t*)fileBytes;

	uint32_t magic = 0, headerSize = 0, flags = 0, height = 0, width = 0;
	uint32_t pitchOrLinear = 0, depth = 0, mipMapCount = 0;
	if (!dds_read_u32(data, fileSize, 0, magic) || magic != dds_fourcc('D', 'D', 'S', ' ')
		|| !dds_read_u32(data, fileSize, 4, headerSize) || headerSize != 124
		|| !dds_read_u32(data, fileSize, 8, flags)
		|| !dds_read_u32(data, fileSize, 12, height)
		|| !dds_read_u32(data, fileSize, 16, width)
		|| !dds_read_u32(data, fileSize, 20, pitchOrLinear)
		|| !dds_read_u32(data, fileSize, 24, depth)
		|| !dds_read_u32(data, fileSize, 28, mipMapCount))
	{
		reason = "truncated or nonstandard dds header";
		return DdsDecode_Corrupt;
	}
	if (width == 0 || height == 0 || width > 16384 || height > 16384)
	{
		reason = "dds dimensions are zero or above 16384";
		return DdsDecode_Unsupported;
	}

	uint32_t pfSize = 0, pfFlags = 0, fourcc = 0, bitCount = 0;
	uint32_t rMask = 0, gMask = 0, bMask = 0, aMask = 0, caps2 = 0;
	if (!dds_read_u32(data, fileSize, 76, pfSize) || pfSize != 32
		|| !dds_read_u32(data, fileSize, 80, pfFlags)
		|| !dds_read_u32(data, fileSize, 84, fourcc)
		|| !dds_read_u32(data, fileSize, 88, bitCount)
		|| !dds_read_u32(data, fileSize, 92, rMask)
		|| !dds_read_u32(data, fileSize, 96, gMask)
		|| !dds_read_u32(data, fileSize, 100, bMask)
		|| !dds_read_u32(data, fileSize, 104, aMask)
		|| !dds_read_u32(data, fileSize, 112, caps2))
	{
		reason = "truncated dds pixelformat";
		return DdsDecode_Corrupt;
	}

	const uint32_t DDSD_PITCH = 0x8;
	const uint32_t DDSD_MIPMAPCOUNT = 0x20000;
	const uint32_t DDSCAPS2_CUBEMAP = 0x200;
	const uint32_t DDSCAPS2_VOLUME = 0x200000;
	const uint32_t DDPF_FOURCC = 0x4;
	const uint32_t DDPF_RGB = 0x40;
	const uint32_t DDS_DIMENSION_TEXTURE2D = 3;
	const uint32_t DDS_DIMENSION_TEXTURE3D = 4;

	if ((caps2 & DDSCAPS2_VOLUME) || depth > 1)
	{
		reason = "volume dds is not handled by the pageable path";
		return DdsDecode_Unsupported;
	}

	bool isCube = (caps2 & DDSCAPS2_CUBEMAP) != 0;
	uint32_t format = 0;
	uint32_t arrayElements = 1;
	uint32_t pixelOffset = 128;

	if ((pfFlags & DDPF_FOURCC) && fourcc == dds_fourcc('D', 'X', '1', '0'))
	{
		uint32_t dimension = 0, miscFlag = 0, dx10Array = 0;
		if (!dds_read_u32(data, fileSize, 128, format)
			|| !dds_read_u32(data, fileSize, 132, dimension)
			|| !dds_read_u32(data, fileSize, 136, miscFlag)
			|| !dds_read_u32(data, fileSize, 140, dx10Array))
		{
			reason = "truncated dx10 dds header";
			return DdsDecode_Corrupt;
		}
		if (dimension == DDS_DIMENSION_TEXTURE3D)
		{
			reason = "dx10 volume texture";
			return DdsDecode_Unsupported;
		}
		if (dimension != DDS_DIMENSION_TEXTURE2D)
		{
			reason = "dx10 resource is not a 2d texture";
			return DdsDecode_Unsupported;
		}
		uint32_t dummyB = 0, dummyW = 0, dummyH = 0;
		if (!dds_format_metrics(format, dummyB, dummyW, dummyH))
		{
			reason = "dx10 format is not a straight copy";
			return DdsDecode_Unsupported;
		}
		if (dx10Array == 0)
			dx10Array = 1;
		isCube = (miscFlag & DdsMisc_TextureCube) != 0;
		if (isCube)
		{
			if (dx10Array > 32)
			{
				reason = "cube array is too large for the pageable path";
				return DdsDecode_Unsupported;
			}
			arrayElements = dx10Array * 6;
		}
		else
		{
			if (dx10Array > 64)
			{
				reason = "texture array is too large for the pageable path";
				return DdsDecode_Unsupported;
			}
			arrayElements = dx10Array;
		}
		pixelOffset = 148;
	}
	else if ((pfFlags & DDPF_FOURCC) && dds_legacy_fourcc_format(fourcc, format))
	{
		arrayElements = isCube ? 6 : 1;
	}
	else if ((pfFlags & DDPF_RGB) && dds_legacy_rgb_format(bitCount, rMask, gMask, bMask, aMask, format))
	{
		arrayElements = isCube ? 6 : 1;
	}
	else
	{
		reason = "dds format is not a straight gpu copy";
		return DdsDecode_Unsupported;
	}

	if (expectCube != isCube)
	{
		reason = expectCube ? "expected a cubemap" : "did not expect a cubemap";
		return DdsDecode_Unsupported;
	}
	if (pixelOffset > fileSize)
	{
		reason = "dds payload is missing";
		return DdsDecode_Corrupt;
	}

	uint32_t fileMips = 1;
	if (flags & DDSD_MIPMAPCOUNT)
		fileMips = mipMapCount == 0 ? 1 : mipMapCount;
	const uint32_t possibleMips = dds_max_mip_count(width, height);
	if (fileMips > possibleMips)
		fileMips = possibleMips;
	if (fileMips == 0 || fileMips > 16)
	{
		reason = "mip count is not usable";
		return DdsDecode_Unsupported;
	}

	uint32_t keptW = width, keptH = height, keptMips = fileMips, skipped = 0;
	dds_apply_mip_skip(width, height, fileMips, mipSkip, keptW, keptH, keptMips, skipped);

	uint32_t blockBytes = 0, bw = 1, bh = 1;
	dds_format_metrics(format, blockBytes, bw, bh);
	const bool compressed = bw != 1;

	uint32_t tightRow = 0, tightSlice = 0;
	if (!dds_subresource_layout(format, width, height, tightRow, tightSlice))
	{
		reason = "top mip does not fit";
		return DdsDecode_Corrupt;
	}

	// Some uncompressed single-mip files store DWORD-padded rows and put that
	// stride in dwPitchOrLinearSize. Compressed files use that field as a
	// byte size, so it must not be treated as a pitch.
	uint32_t paddedRow = 0;
	const bool canPad = !compressed && fileMips == 1 && arrayElements == 1 && (flags & DDSD_PITCH)
		&& pitchOrLinear > tightRow;
	if (canPad)
	{
		const uint64_t paddedSlice = (uint64_t)pitchOrLinear * (uint64_t)height;
		if (paddedSlice > 0xffffffffu || pixelOffset + (uint32_t)paddedSlice < pixelOffset
			|| pixelOffset + (uint32_t)paddedSlice > fileSize)
		{
			reason = "uncompressed pitch does not fit the file";
			return DdsDecode_Unsupported;
		}
		paddedRow = pitchOrLinear;
	}

	uint32_t src = pixelOffset;
	uint32_t dst = 0;
	uint32_t subCount = 0;
	for (uint32_t element = 0; element < arrayElements; ++element)
	{
		uint32_t mipW = width;
		uint32_t mipH = height;
		for (uint32_t mip = 0; mip < fileMips; ++mip)
		{
			uint32_t rowPitch = 0;
			uint32_t slicePitch = 0;
			if (!dds_subresource_layout(format, mipW, mipH, rowPitch, slicePitch))
			{
				reason = "mip layout overflow";
				return DdsDecode_Corrupt;
			}
			if (paddedRow && mip == 0)
			{
				rowPitch = paddedRow;
				slicePitch = paddedRow * height;
			}
			if (src > fileSize || fileSize - src < slicePitch)
			{
				reason = "dds payload is shorter than its mip chain";
				return DdsDecode_Corrupt;
			}
			if (mip >= skipped)
			{
				if (subCount >= DdsPlan_MaxSubs)
				{
					reason = "too many subresources";
					return DdsDecode_Unsupported;
				}
				DdsPlanSubresource& sub = plan.subs[subCount++];
				sub.fileOffset = src;
				sub.packedOffset = dst;
				sub.length = slicePitch;
				sub.rowPitch = rowPitch;
				sub.slicePitch = slicePitch;
				if (dst + slicePitch < dst)
				{
					reason = "packed texture size overflow";
					return DdsDecode_Corrupt;
				}
				dst += slicePitch;
			}
			src += slicePitch;
			mipW = mipW > 1 ? mipW / 2 : 1;
			mipH = mipH > 1 ? mipH / 2 : 1;
		}
	}

	if (subCount == 0 || dst == 0)
	{
		reason = "dds produced no subresources";
		return DdsDecode_Corrupt;
	}

	plan.format = format;
	plan.width = keptW;
	plan.height = keptH;
	plan.mipCount = keptMips;
	plan.arraySize = arrayElements;
	plan.miscFlags = isCube ? DdsMisc_TextureCube : 0;
	plan.byteCount = dst;
	plan.subCount = subCount;
	reason = nullptr;
	return DdsDecode_Ok;
}

inline void dds_apply_plan(const void* fileBytes, const DdsDecodePlan& plan, uint8_t* dst)
{
	const uint8_t* src = (const uint8_t*)fileBytes;
	for (uint32_t i = 0; i < plan.subCount; ++i)
	{
		const DdsPlanSubresource& sub = plan.subs[i];
		memcpy(dst + sub.packedOffset, src + sub.fileOffset, sub.length);
	}
}
