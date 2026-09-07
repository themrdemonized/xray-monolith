#ifndef svp_cameraH
#define svp_cameraH
#pragma once

// pip build Device.matrices[1] (the SVP camera) from the captured lens and the weapon zoom,
// called from renderGBuffer after the hud derive
void svpCamera();

// pip snapshot HUD geometry centers into the module's geomscan list before render_hud clears
// the lists, the caller keeps its gate inline
void svp_snapshot_hud_geom();

#endif
