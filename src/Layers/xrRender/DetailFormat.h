#pragma once

#ifndef _DETAIL_FORMAT_H_
#define _DETAIL_FORMAT_H_
#pragma pack(push,1)

// Detail format: v3 (legacy, 6-bit ids, 16B slot) + v4 (14-bit ids, 20B slot).
// Layout is shared byte-for-byte across engine, SDK and the details compiler.
#define DETAIL_VERSION_3	3			// legacy: 6-bit ids, 16-byte slot
#define DETAIL_VERSION_4	4			// new:    14-bit ids, 20-byte slot
#define DETAIL_VERSION		DETAIL_VERSION_4	// default for newly written files

#define DETAIL_SLOT_SIZE	2.f
#define DETAIL_SLOT_SIZE_2	DETAIL_SLOT_SIZE*0.5f

//	int s_x	= iFloor			(EYE.x/slot_size+.5f)+offs_x;		// [0...size_x)
//	int s_z	= iFloor			(EYE.z/slot_size+.5f)+offs_z;		// [0...size_z)

/*
0 - Header(version,obj_count,size_x,size_z,min_x,min_z)
1 - Objects
	0
	1
	..
	obj_count-1
2 - slots
*/

#define DO_NO_WAVING	0x0001

struct DetailHeader
{
	u32 version;
	u32 object_count;
	int offs_x, offs_z;
	u32 size_x, size_z;
};

struct DetailPalette
{
	u16 a0:4;
	u16 a1:4;
	u16 a2:4;
	u16 a3:4;
};

// LEGACY v3 slot — 16 bytes, 6-bit ids. Used only to read/expand v3 files.
struct DetailSlot_v3
{
	u32 y_base  : 12;
	u32 y_height: 8;
	u32 id0     : 6;	// 0x3F = empty
	u32 id1     : 6;
	u32 id2     : 6;
	u32 id3     : 6;
	u32 c_dir   : 4;
	u32 c_hemi  : 4;
	u32 c_r     : 4;
	u32 c_g     : 4;
	u32 c_b     : 4;
	DetailPalette palette[4];
	enum { ID_Empty = 0x3f };
};

// v4 slot — 20 bytes, 14-bit ids. Fields reordered to pack into 3 x u32 with no
// waste. Also the in-memory working type the engine renders from.
struct DetailSlot
{
	// --- u32 #0 : 14 + 14 + 4 = 32 ---
	u32 id0     : 14;	// 0x3FFF = empty
	u32 id1     : 14;	// 0x3FFF = empty
	u32 c_dir   : 4;	// 0..1 q
	// --- u32 #1 : 14 + 14 + 4 = 32 ---
	u32 id2     : 14;	// 0x3FFF = empty
	u32 id3     : 14;	// 0x3FFF = empty
	u32 c_hemi  : 4;	// 0..1 q
	// --- u32 #2 : 12 + 8 + 4 + 4 + 4 = 32 ---
	u32 y_base  : 12;	// 1 unit = 20 cm, low = -200m, high = 4096*20cm - 200 = 619.2m
	u32 y_height: 8;	// 1 unit = 10 cm, low = 0,     high = 256*10 ~= 25.6m
	u32 c_r     : 4;	// rgb = 4.4.4
	u32 c_g     : 4;
	u32 c_b     : 4;
	DetailPalette palette[4];
public:
	enum { ID_Empty = 0x3fff };

public:
	void w_y(float base, float height)
	{
		s32 _base = iFloor((base + 200) / .2f);
		clamp(_base, 0, 4095);
		y_base = _base;
		f32 _error = base - r_ybase();
		s32 _height = iCeil((height + _error) / .1f);
		clamp(_height, 0, 255);
		y_height = _height;
	}

	float r_ybase() { return float(y_base) * .2f - 200.f; }
	float r_yheight() { return float(y_height) * .1f; }

	u32 w_qclr(float v, u32 range)
	{
		s32 _v = iFloor(v * float(range));
		clamp(_v, 0, s32(range));
		return _v;
	};
	float r_qclr(u32 v, u32 range) { return float(v) / float(range); }

	void color_editor()
	{
		c_dir = w_qclr(0.5f, 15);
		c_hemi = w_qclr(0.5f, 15);
		c_r = w_qclr(0.f, 15);
		c_g = w_qclr(0.f, 15);
		c_b = w_qclr(0.f, 15);
	}

	u16 r_id(u32 idx)
	{
		switch (idx)
		{
		case 0: return (u16)id0;
		case 1: return (u16)id1;
		case 2: return (u16)id2;
		case 3: return (u16)id3;
		default: NODEFAULT;
		}
#ifdef DEBUG
		return 0;
#endif
	}

	void w_id(u32 idx, u16 val)
	{
		switch (idx)
		{
		case 0: id0 = val;
			break;
		case 1: id1 = val;
			break;
		case 2: id2 = val;
			break;
		case 3: id3 = val;
			break;
		default: NODEFAULT;
		}
	}
};

static_assert(sizeof(DetailSlot_v3) == 16, "DetailSlot_v3 must be 16 bytes");
static_assert(sizeof(DetailSlot)    == 20, "DetailSlot must pack to 20 bytes");

// v3 <-> v4 conversion (used only at the file I/O boundary).
// Every field copies 1:1; only the empty-id sentinel is remapped.
inline u16 _detail_id_v3_to_v4(u32 v) { return (v == DetailSlot_v3::ID_Empty) ? (u16)DetailSlot::ID_Empty : (u16)v; }
inline u32 _detail_id_v4_to_v3(u32 v) { return (v == DetailSlot::ID_Empty)    ? (u32)DetailSlot_v3::ID_Empty : (v & 0x3f); }

inline void expand_v3(DetailSlot& d, const DetailSlot_v3& s)	// read path: v3 -> working
{
	d.y_base = s.y_base;  d.y_height = s.y_height;
	d.id0 = _detail_id_v3_to_v4(s.id0);  d.id1 = _detail_id_v3_to_v4(s.id1);
	d.id2 = _detail_id_v3_to_v4(s.id2);  d.id3 = _detail_id_v3_to_v4(s.id3);
	d.c_dir = s.c_dir;  d.c_hemi = s.c_hemi;
	d.c_r = s.c_r;  d.c_g = s.c_g;  d.c_b = s.c_b;
	for (int i = 0; i < 4; ++i) d.palette[i] = s.palette[i];
}

inline void pack_v3(DetailSlot_v3& d, const DetailSlot& s)	// write path: working -> v3
{
	// only valid when all ids <= 62 (a <=63-object level); caller guarantees this
	d.y_base = s.y_base;  d.y_height = s.y_height;
	d.id0 = _detail_id_v4_to_v3(s.id0);  d.id1 = _detail_id_v4_to_v3(s.id1);
	d.id2 = _detail_id_v4_to_v3(s.id2);  d.id3 = _detail_id_v4_to_v3(s.id3);
	d.c_dir = s.c_dir;  d.c_hemi = s.c_hemi;
	d.c_r = s.c_r;  d.c_g = s.c_g;  d.c_b = s.c_b;
	for (int i = 0; i < 4; ++i) d.palette[i] = s.palette[i];
}

#pragma pack(pop)
#endif //_DETAIL_FORMAT_H_
