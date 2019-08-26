#pragma once

// Õðàíèëèùå âíåøíèõ øåéäåðíûõ ïàðàìåòðîâ, êîòîðûå ÷èòàþòñÿ â Blender_Recorder_StandartBinding.cpp
class ShadersExternalData //--#SM+#--
{
public:
	Fmatrix m_script_params; // Ìàòðèöà, çíà÷åíèÿ êîòîðîé äîñòóïíû èç Lua
	Fvector4 hud_params;     // [zoom_rotate_factor, secondVP_zoom_factor, hud_fov, NULL] - Ïàðàìåòðû õóäà îðóæèÿ
	Fvector4 m_blender_mode; // x\y = [0 - default, 1 - night vision, 2 - thermo vision, ... ñì. common.h] - Ðåæèìû ðåíäåðèíãà
							 // x - îñíîâíîé âüþïîðò, y - âòîðîé âüþïîðò, z = ?, w = [0 - èä¸ò ðåíäåð îáû÷íîãî îáúåêòà, 1 - èä¸ò ðåíäåð äåòàëüíûõ îáúåêòîâ (òðàâà, ìóñîð)]

	ShadersExternalData()
	{
		m_script_params = Fmatrix();
		hud_params.set(0.f, 0.f, 0.f, 0.f);
		m_blender_mode.set(0.f, 0.f, 0.f, 0.f);
	}
}; 