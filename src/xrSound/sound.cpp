#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_CoreA.h"

XRSOUND_API xr_token* snd_devices_token = NULL;
XRSOUND_API xr_string snd_device_name;

void CSound_manager_interface::_create(int stage)
{
	if (stage == 0)
	{
		SoundRenderA = xr_new<CSoundRender_CoreA>();
		SoundRender = SoundRenderA;
		Sound = SoundRender;

		if (Core.ParamsData.test(ECoreParams::nosound))
		{
			SoundRender->bPresent = FALSE;
			return;
		}
		else
			SoundRender->bPresent = TRUE;
	}

	if (!SoundRender->bPresent) return;
	Sound->_initialize(stage);
}

void CSound_manager_interface::_destroy()
{
	if (Sound)
		Sound->stop_persistent_emitters();
	Sound->_clear();
	xr_delete(SoundRender);
	Sound = 0;
}
