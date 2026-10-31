#ifndef BUS_MASK_H
#define BUS_MASK_H
// shader bus mask target, tag times 2^-24, reads 0 while no target exists
Texture2D<float> bus_engine_mask;
uint bus_mask_bits(int2 p) { return (uint)(bus_engine_mask.Load(int3(p, 0)) * 16777216.0); }
#endif
