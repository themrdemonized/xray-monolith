// Standalone check for dds_pageable_decode.h. Not part of the MSVC engine
// projects. Build with:
//   g++ -std=c++17 -Wall -Wextra -Werror -o dds_pageable_decode_test dds_pageable_decode_test.cpp
// Pass a real .dds path as argv[1] to check that file too.

#include "dds_pageable_decode.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int g_fails = 0;

static void expect(bool cond, const char* message)
{
	if (!cond)
	{
		std::fprintf(stderr, "FAIL: %s\n", message);
		++g_fails;
	}
}

static void write_u32(std::vector<uint8_t>& out, uint32_t value)
{
	out.push_back((uint8_t)(value & 0xff));
	out.push_back((uint8_t)((value >> 8) & 0xff));
	out.push_back((uint8_t)((value >> 16) & 0xff));
	out.push_back((uint8_t)((value >> 24) & 0xff));
}

static std::vector<uint8_t> make_legacy_header(uint32_t width, uint32_t height, uint32_t mips, uint32_t pitchOrLinear,
	uint32_t flags, uint32_t pfFlags, uint32_t fourcc, uint32_t bitCount, uint32_t rMask, uint32_t gMask, uint32_t bMask,
	uint32_t aMask, uint32_t caps2)
{
	std::vector<uint8_t> h;
	h.reserve(128);
	write_u32(h, dds_fourcc('D', 'D', 'S', ' '));
	write_u32(h, 124);
	write_u32(h, flags);
	write_u32(h, height);
	write_u32(h, width);
	write_u32(h, pitchOrLinear);
	write_u32(h, 0);
	write_u32(h, mips);
	for (int i = 0; i < 11; ++i)
		write_u32(h, 0);
	write_u32(h, 32);
	write_u32(h, pfFlags);
	write_u32(h, fourcc);
	write_u32(h, bitCount);
	write_u32(h, rMask);
	write_u32(h, gMask);
	write_u32(h, bMask);
	write_u32(h, aMask);
	write_u32(h, 0x1000);
	write_u32(h, caps2);
	write_u32(h, 0);
	write_u32(h, 0);
	write_u32(h, 0);
	expect(h.size() == 128, "legacy header is 128 bytes");
	return h;
}

static void append_bytes(std::vector<uint8_t>& dst, uint32_t count, uint8_t fill)
{
	dst.insert(dst.end(), count, fill);
}

static void test_cursor_like()
{
	const uint32_t DDSD = 0x100f;
	const uint32_t DDPF_RGB = 0x41;
	std::vector<uint8_t> file = make_legacy_header(256, 256, 1, 1024, DDSD, DDPF_RGB, 0, 32,
		0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000, 0);
	append_bytes(file, 256u * 256u * 4u, 0);
	file[128] = 0x11;
	file[129] = 0x22;
	file[130] = 0x33;
	file[131] = 0x44;
	file.back() = 0xab;

	DdsDecodePlan plan;
	const char* reason = nullptr;
	const DdsDecodeStatus st = dds_plan_pageable(file.data(), (uint32_t)file.size(), 0, false, plan, reason);
	expect(st == DdsDecode_Ok, "A8R8G8B8 decodes");
	expect(plan.format == DdsFmt_B8G8R8A8_UNORM, "A8R8G8B8 is BGRA8");
	expect(plan.width == 256 && plan.height == 256 && plan.mipCount == 1, "256 square, one mip");
	expect(plan.arraySize == 1 && plan.miscFlags == 0, "not a cube");
	expect(plan.byteCount == 256u * 256u * 4u, "tight byte count");
	expect(plan.subCount == 1 && plan.subs[0].rowPitch == 1024, "row pitch 1024");

	std::vector<uint8_t> packed(plan.byteCount);
	dds_apply_plan(file.data(), plan, packed.data());
	expect(packed[0] == 0x11 && packed[1] == 0x22 && packed[2] == 0x33 && packed[3] == 0x44, "first texel preserved");
	expect(packed.back() == 0xab, "last byte preserved");
}

static void test_bc1_mips_and_skip()
{
	// 8x8 BC1, 4 mips: 32 + 8 + 8 + 8 = 56.
	// DDSD_CAPS|HEIGHT|WIDTH|PIXELFORMAT|MIPMAPCOUNT|LINEARSIZE
	const uint32_t DDSD = 0x000A1007;
	std::vector<uint8_t> file = make_legacy_header(8, 8, 4, 32, DDSD, 0x4, dds_fourcc('D', 'X', 'T', '1'), 0, 0, 0, 0, 0, 0);
	const uint32_t sizes[4] = {32, 8, 8, 8};
	for (uint32_t mip = 0; mip < 4; ++mip)
		append_bytes(file, sizes[mip], (uint8_t)(0x10 + mip));

	DdsDecodePlan full;
	const char* reason = nullptr;
	expect(dds_plan_pageable(file.data(), (uint32_t)file.size(), 0, false, full, reason) == DdsDecode_Ok, "bc1 full chain");
	expect(full.format == DdsFmt_BC1_UNORM, "dxt1 -> bc1");
	expect(full.mipCount == 4 && full.width == 8 && full.byteCount == 56 && full.subCount == 4, "bc1 full geometry");
	expect(full.subs[0].rowPitch == 16 && full.subs[0].slicePitch == 32, "bc1 top pitch");
	expect(full.subs[1].slicePitch == 8 && full.subs[3].slicePitch == 8, "bc1 lower mips are one block");

	std::vector<uint8_t> packed(full.byteCount);
	dds_apply_plan(file.data(), full, packed.data());
	expect(packed[0] == 0x10 && packed[32] == 0x11 && packed[40] == 0x12 && packed[48] == 0x13, "mip bytes stay in order");

	DdsDecodePlan skipped;
	expect(dds_plan_pageable(file.data(), (uint32_t)file.size(), 2, false, skipped, reason) == DdsDecode_Ok, "bc1 skip 2");
	expect(skipped.width == 2 && skipped.height == 2 && skipped.mipCount == 2, "skip lands on the 2x2 mip");
	expect(skipped.byteCount == 16 && skipped.subCount == 2, "two remaining blocks");
	std::vector<uint8_t> tail(skipped.byteCount);
	dds_apply_plan(file.data(), skipped, tail.data());
	expect(tail[0] == 0x12 && tail[8] == 0x13, "skip drops the larger mips only");

	DdsDecodePlan floor;
	expect(dds_plan_pageable(file.data(), (uint32_t)file.size(), 99, false, floor, reason) == DdsDecode_Ok, "excessive skip");
	expect(floor.mipCount == 1 && floor.width == 1 && floor.height == 1 && floor.byteCount == 8, "skip keeps the last mip");
}

static void test_cube()
{
	const uint32_t DDSD = 0x000A1007;
	const uint32_t cubeCaps = 0x200 | 0xfe00;
	std::vector<uint8_t> file = make_legacy_header(4, 4, 1, 8, DDSD, 0x4, dds_fourcc('D', 'X', 'T', '5'), 0, 0, 0, 0, 0, cubeCaps);
	for (uint32_t face = 0; face < 6; ++face)
		append_bytes(file, 16, (uint8_t)(0x30 + face));

	DdsDecodePlan plan;
	const char* reason = nullptr;
	expect(dds_plan_pageable(file.data(), (uint32_t)file.size(), 0, true, plan, reason) == DdsDecode_Ok, "cube decodes");
	expect(plan.arraySize == 6 && plan.miscFlags == DdsMisc_TextureCube && plan.subCount == 6, "six faces");
	expect(plan.format == DdsFmt_BC3_UNORM && plan.byteCount == 96, "dxt5 cube size");
	DdsDecodePlan rejected;
	expect(dds_plan_pageable(file.data(), (uint32_t)file.size(), 0, false, rejected, reason) == DdsDecode_Unsupported, "2d path rejects cube");

	std::vector<uint8_t> packed(plan.byteCount);
	dds_apply_plan(file.data(), plan, packed.data());
	expect(packed[0] == 0x30 && packed[16] == 0x31 && packed[80] == 0x35, "face order preserved");
}

static void test_cube_mip_skip_is_per_face()
{
	// 4x4 BC1 cubemap, two mips. Each face is 8 + 8 bytes, and skipping the
	// top mip must drop that mip inside every face, not a prefix of the file.
	const uint32_t DDSD = 0x000A1007;
	std::vector<uint8_t> file = make_legacy_header(4, 4, 2, 8, DDSD, 0x4, dds_fourcc('D', 'X', 'T', '1'), 0, 0, 0, 0, 0, 0xfe00);
	for (uint32_t face = 0; face < 6; ++face)
	{
		append_bytes(file, 8, (uint8_t)(0x40 + face));
		append_bytes(file, 8, (uint8_t)(0x50 + face));
	}

	DdsDecodePlan full;
	const char* reason = nullptr;
	expect(dds_plan_pageable(file.data(), (uint32_t)file.size(), 0, true, full, reason) == DdsDecode_Ok, "cube mips decode");
	expect(full.subCount == 12 && full.mipCount == 2 && full.byteCount == 96, "cube mip geometry");
	std::vector<uint8_t> packed(full.byteCount);
	dds_apply_plan(file.data(), full, packed.data());
	expect(packed[0] == 0x40 && packed[8] == 0x50 && packed[16] == 0x41 && packed[24] == 0x51, "cube mips stay face-major");

	DdsDecodePlan skipped;
	expect(dds_plan_pageable(file.data(), (uint32_t)file.size(), 1, true, skipped, reason) == DdsDecode_Ok, "cube mip skip");
	expect(skipped.mipCount == 1 && skipped.width == 2 && skipped.height == 2 && skipped.subCount == 6, "cube skip keeps one mip per face");
	expect(skipped.byteCount == 48, "cube skip size");
	std::vector<uint8_t> tail(skipped.byteCount);
	dds_apply_plan(file.data(), skipped, tail.data());
	expect(tail[0] == 0x50 && tail[8] == 0x51 && tail[40] == 0x55, "cube skip gathers the small mip of each face");
}

static void test_padded_uncompressed_rows()
{
	// 2x2 R8 with pitch 4. Tight layout would be 4 bytes; the file stores 8.
	const uint32_t flags = 0x1007 | 0x8;
	std::vector<uint8_t> file = make_legacy_header(2, 2, 1, 4, flags, 0x4, dds_fourcc('D', 'X', '1', '0'), 0, 0, 0, 0, 0, 0);
	write_u32(file, DdsFmt_R8_UNORM);
	write_u32(file, 3);
	write_u32(file, 0);
	write_u32(file, 1);
	write_u32(file, 0);
	const uint8_t rows[] = {0x11, 0x22, 0xaa, 0xbb, 0x33, 0x44, 0xcc, 0xdd};
	file.insert(file.end(), rows, rows + 8);

	DdsDecodePlan plan;
	const char* reason = nullptr;
	expect(dds_plan_pageable(file.data(), (uint32_t)file.size(), 0, false, plan, reason) == DdsDecode_Ok, "padded r8 decodes");
	expect(plan.format == DdsFmt_R8_UNORM && plan.subCount == 1, "padded r8 format");
	expect(plan.subs[0].rowPitch == 4 && plan.subs[0].slicePitch == 8 && plan.byteCount == 8, "padded pitch kept");
	std::vector<uint8_t> packed(plan.byteCount);
	dds_apply_plan(file.data(), plan, packed.data());
	expect(packed[0] == 0x11 && packed[1] == 0x22 && packed[4] == 0x33 && packed[5] == 0x44, "padded rows preserved");
}

static void test_dx10_bc7_and_rejects()
{
	std::vector<uint8_t> file = make_legacy_header(4, 4, 1, 16, 0x00081007, 0x4, dds_fourcc('D', 'X', '1', '0'), 0, 0, 0, 0, 0, 0);
	write_u32(file, DdsFmt_BC7_UNORM);
	write_u32(file, 3); // texture2d
	write_u32(file, 0);
	write_u32(file, 1);
	write_u32(file, 0);
	append_bytes(file, 16, 0x7c);
	expect(file.size() == 148 + 16, "dx10 header plus one block");

	DdsDecodePlan plan;
	const char* reason = nullptr;
	expect(dds_plan_pageable(file.data(), (uint32_t)file.size(), 0, false, plan, reason) == DdsDecode_Ok, "bc7 dx10 decodes");
	expect(plan.format == DdsFmt_BC7_UNORM && plan.byteCount == 16 && plan.subs[0].rowPitch == 16, "bc7 layout");

	std::vector<uint8_t> shortFile(file.begin(), file.end() - 4);
	expect(dds_plan_pageable(shortFile.data(), (uint32_t)shortFile.size(), 0, false, plan, reason) == DdsDecode_Corrupt, "truncated payload");

	std::vector<uint8_t> volume = make_legacy_header(4, 4, 1, 8, 0x00081007, 0x4, dds_fourcc('D', 'X', 'T', '1'), 0, 0, 0, 0, 0, 0x200000);
	append_bytes(volume, 8, 1);
	expect(dds_plan_pageable(volume.data(), (uint32_t)volume.size(), 0, false, plan, reason) == DdsDecode_Unsupported, "volume rejected");

	std::vector<uint8_t> odd = make_legacy_header(4, 4, 1, 8, 0x00081007, 0x4, dds_fourcc('F', 'O', 'O', ' '), 0, 0, 0, 0, 0, 0);
	append_bytes(odd, 8, 1);
	expect(dds_plan_pageable(odd.data(), (uint32_t)odd.size(), 0, false, plan, reason) == DdsDecode_Unsupported, "unknown fourcc rejected");

	uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	expect(dds_plan_pageable(junk, 8, 0, false, plan, reason) == DdsDecode_Corrupt, "bad magic rejected");
}

static void test_real_file(const char* path)
{
	FILE* f = std::fopen(path, "rb");
	expect(f != nullptr, "open real dds");
	if (!f)
		return;
	std::fseek(f, 0, SEEK_END);
	const long bytes = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	expect(bytes > 0, "real dds size");
	std::vector<uint8_t> file((size_t)bytes);
	expect(std::fread(file.data(), 1, file.size(), f) == file.size(), "read real dds");
	std::fclose(f);

	DdsDecodePlan plan;
	const char* reason = nullptr;
	const DdsDecodeStatus st = dds_plan_pageable(file.data(), (uint32_t)file.size(), 0, false, plan, reason);
	if (st != DdsDecode_Ok)
		std::fprintf(stderr, "real file rejected: %s\n", reason ? reason : "?");
	expect(st == DdsDecode_Ok, "real dds decodes");
	expect(plan.format == DdsFmt_B8G8R8A8_UNORM, "cursor dds is BGRA8");
	expect(plan.width == 256 && plan.height == 256 && plan.byteCount == 256u * 256u * 4u, "cursor geometry");
	std::vector<uint8_t> packed(plan.byteCount);
	dds_apply_plan(file.data(), plan, packed.data());
	expect(std::memcmp(packed.data(), file.data() + (file.size() - plan.byteCount), plan.byteCount) == 0, "cursor pixels match the file");
}

int main(int argc, char** argv)
{
	test_cursor_like();
	test_bc1_mips_and_skip();
	test_cube();
	test_cube_mip_skip_is_per_face();
	test_padded_uncompressed_rows();
	test_dx10_bc7_and_rejects();
	if (argc > 1)
		test_real_file(argv[1]);
	if (g_fails)
	{
		std::fprintf(stderr, "%d failure(s)\n", g_fails);
		return 1;
	}
	std::printf("dds_pageable_decode: ok\n");
	return 0;
}
