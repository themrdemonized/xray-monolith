#ifndef BUS_HUD_MASK_H
#define BUS_HUD_MASK_H
#include "bus_mask.h"
#define BUS_HUD_ARMS      0x001
#define BUS_HUD_ARMS2     0x002
#define BUS_HUD_ITEM0     0x004
#define BUS_HUD_ITEM1     0x008
#define BUS_HUD_SCOPE     0x010
#define BUS_HUD_PROP      0x020
#define BUS_HUD_ATT_ACTOR 0x040
#define BUS_HUD_ATT_ITEM  0x080
#define BUS_HUD_CAM       0x100
// set with the owner's bit where a 3D lens put world depth back
#define BUS_HUD_LENS      0x200
#define BUS_HUD_ACTOR     0x043
#define BUS_HUD_ITEMS     0x1BC
#define BUS_HUD_ALL       0x1FF
// x is 1 while the mask target holds this frame's HUD bits
float4 bus_engine_hud_mask;
bool hud_mask_live() { return bus_engine_hud_mask.x > 0; }
bool hud_all(int2 p) { return hud_mask_live() && (bus_mask_bits(p) & BUS_HUD_ALL) != 0; }
bool hud_items(int2 p) { return hud_mask_live() && (bus_mask_bits(p) & BUS_HUD_ITEMS) != 0; }
bool hud_actor(int2 p) { return hud_mask_live() && (bus_mask_bits(p) & BUS_HUD_ACTOR) != 0; }
#endif
