// Clip-space geometry: no camera/world lighting or UI pixel transform.
struct vin { float3 position : POSITION; float4 color : COLOR0; float2 uv : TEXCOORD0; };
struct vout { float4 position : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; };
vout main(vin I)
{
    vout O;
    O.position = float4(I.position, 1.0);
    O.color = I.color.bgra;
    O.uv = I.uv;
    return O;
}
