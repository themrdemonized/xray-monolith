#pragma once

// Generic render-only descriptors. No addon names, gameplay, input or ALife.
struct R1ViewParameters
{
    u32 width = 640, height = 360, interval = 1, quality = 1, output = 0;
    bool relative = true, primary = false, hud = false, clip = false, flip_x = false, actor_body = false;
    bool transform_camera = false;
    Fmatrix camera_transform;
    Fvector position, direction, up, offset;
    Fvector4 plane, rect;
    Fvector quad_position, quad_right, quad_up;
    float fov = 0, aspect = 0, far_distance = 0, projection_shift = 0;
    R1ViewParameters()
    {
        camera_transform.identity();
        position.set(0,0,0); direction.set(0,0,1); up.set(0,1,0); offset.set(0,0,0);
        plane.set(0,0,1,0); rect.set(0,0,1,1);
        quad_position.set(0,0,0); quad_right.set(1,0,0); quad_up.set(0,1,0);
    }
};
struct R1ViewVertex
{
    Fvector position;
    Fvector2 uv;
    u32 material=0;
};
struct R1ViewGeometry
{
    xr_vector<R1ViewVertex> vertices;
    shared_str textures[2];
};
struct R1ViewStatus
{
    u32 width = 0, height = 0, renders = 0, frame = 0, draws = 0;
    bool ready = false;
    int error = 0;
    float render_ms = 0, total_ms = 0;
};
