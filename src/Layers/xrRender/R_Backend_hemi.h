#ifndef r_backend_hemiH
#define r_backend_hemiH
#pragma once

class ECORE_API R_hemi
{
public:
	R_constant* c_pos_faces;
	R_constant* c_neg_faces;
	R_constant* c_material;

	//--DSR-- HeatVision_start
	R_constant* c_hotness;
	//--DSR-- HeatVision_end

public:
	R_hemi();
	void unmap();

	void set_c_pos_faces(R_constant* C) { c_pos_faces = C; }
	void set_c_neg_faces(R_constant* C) { c_neg_faces = C; }
	void set_c_material(R_constant* C) { c_material = C; }

	//--DSR-- HeatVision_start
	void set_c_hotness(R_constant* C) { c_hotness = C; }
	//--DSR-- HeatVision_end


	void set_pos_faces(float posx, float posy, float posz);
	void set_neg_faces(float negx, float negy, float negz);
	void set_material(float x, float y, float z, float w);

	//--DSR-- HeatVision_start
	void set_hotness(float x, float y, float z, float w);
	//--DSR-- HeatVision_end
};
#endif
