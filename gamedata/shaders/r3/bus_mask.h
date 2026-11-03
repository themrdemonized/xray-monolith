#ifndef BUS_MASK_H
#define BUS_MASK_H
// shader bus mask target, tag times 2^-24, reads 0 while no target exists
Texture2D<float> bus_engine_mask;
uint bus_mask_bits(int2 p) { return (uint)(bus_engine_mask.Load(int3(p, 0)) * 16777216.0); }
// object id target, the game object's id + 1 times 2^-24, 0 for none
Texture2D<float> bus_engine_object_id;
// x is 1 while the id target holds this frame's object ids, a pass that samples bus_engine_object_id makes the engine draw them
float4 bus_engine_mask_ids;
uint bus_mask_object_id(int2 p) { return bus_engine_mask_ids.x > 0 ? (uint)(bus_engine_object_id.Load(int3(p, 0)) * 16777216.0) : 0; }
// mask layer, the layer bits of tagged draws with no depth test, times 2^-24
Texture2D<float> bus_engine_mask_layer;
uint bus_mask_layer_bits(int2 p) { return (uint)(bus_engine_mask_layer.Load(int3(p, 0)) * 16777216.0); }
// particle tag target, the tags of tagged world particles times 2^-24, 0 where none drew
Texture2D<float> bus_engine_particle_mask;
// x is 1 while the particle target holds this frame's particle tags, a pass that samples bus_engine_particle_mask makes the engine draw them
float4 bus_engine_mask_particles;
uint bus_mask_particle_bits(int2 p) { return bus_engine_mask_particles.x > 0 ? (uint)(bus_engine_particle_mask.Load(int3(p, 0)) * 16777216.0) : 0; }
#endif
