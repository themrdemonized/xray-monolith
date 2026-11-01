#ifndef BUS_MASK_H
#define BUS_MASK_H
// shader bus mask target, tag times 2^-24, reads 0 while no target exists
Texture2D<float> bus_engine_mask;
uint bus_mask_bits(int2 p) { return (uint)(bus_engine_mask.Load(int3(p, 0)) * 16777216.0); }
// object id target, the game object's id + 1 times 2^-24, 0 for none
Texture2D<float> bus_engine_object_id;
// x is 1 while the id target holds this frame's object ids, reading it is what makes the engine draw them
float4 bus_engine_mask_ids;
uint bus_mask_object_id(int2 p) { return bus_engine_mask_ids.x > 0 ? (uint)(bus_engine_object_id.Load(int3(p, 0)) * 16777216.0) : 0; }
// mask layer, the layer bits of tagged draws with no depth test, times 2^-24
Texture2D<float> bus_engine_mask_layer;
uint bus_mask_layer_bits(int2 p) { return (uint)(bus_engine_mask_layer.Load(int3(p, 0)) * 16777216.0); }
#endif
