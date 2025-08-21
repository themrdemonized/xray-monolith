#include "stdafx.h"
#pragma hdrstop
#ifdef DEBUG

#include "PHDebug.h"
#include "../xrphysics/iphworld.h"
#endif
#include "alife_space.h"
#include "hit.h"
#include "PHDestroyable.h"
#include "car.h"
#include "actor.h"
#include "cameralook.h"
#include "camerafirsteye.h"
#include "level.h"
#include "../xrEngine/cameramanager.h"

#ifdef CAR_NEW
#include "../Include/xrRender/Kinematics.h"
#endif

bool CCar::HUDView() const
{
	return active_camera->tag == ectFirst;
}

void CCar::cam_Update(float dt, float fov)
{
#ifdef DEBUG
	VERIFY(!physics_world()->Processing());
#endif
	Fvector P, Da;
	Da.set(0, 0, 0);
	//bool							owner = !!Owner();

#ifdef CAR_NEW
	u16 bone_id = (IsCameraZoom() && (m_camera_bone_aim != BI_NONE)) ? m_camera_bone_aim : m_camera_bone_def;
	if (bone_id != BI_NONE)
	{
		Fvector D = Fvector().set(0, 0, 0);
		CCameraBase *cam = Camera();
		switch (cam->tag)
		{
		case ectFirst:
		{
			Fmatrix xfm = Visual()->dcast_PKinematics()->LL_GetTransform(bone_id);
			XFORM().transform_tiny(P, xfm.c);
			if (m_remote_control == false)
			{
				if (OwnerActor())
					OwnerActor()->Orientation().yaw = -cam->yaw;
				if (OwnerActor())
					OwnerActor()->Orientation().pitch = -cam->pitch;
			}
		}
		break;
		case ectChase:
		case ectFree:
		{
			XFORM().transform_tiny(P, m_camera_position);
		}
		break;
		}

		cam->f_fov = fov / (IsCameraZoom() ? m_zoom_factor_aim : m_zoom_factor_def);
		cam->Update(P, D);
		Level().Cameras().UpdateFromCamera(cam);
		return;
	}
#endif

	XFORM().transform_tiny(P, m_camera_position);

	switch (active_camera->tag)
	{
	case ectFirst:
		// rotate head
		if (OwnerActor()) OwnerActor()->Orientation().yaw = -active_camera->yaw;
		if (OwnerActor()) OwnerActor()->Orientation().pitch = -active_camera->pitch;
		break;
	case ectChase: break;
	case ectFree: break;
	}
	active_camera->f_fov = fov;
	active_camera->Update(P, Da);
	Level().Cameras().UpdateFromCamera(active_camera);
}

void CCar::OnCameraChange(int type)
{
	if (Owner())
	{
		if (type == ectFirst)
		{
#ifdef CAR_NEW
			if (m_remote_control)
			{
				Owner()->setVisible(TRUE);
			}
			else
			{
				Owner()->setVisible(FALSE);
			}
#else
			Owner()->setVisible(FALSE);
#endif
		}
		else if (active_camera->tag == ectFirst)
		{
			Owner()->setVisible(TRUE);
		}
	}

	if (!active_camera || active_camera->tag != type)
	{
		active_camera = camera[type];
		if (ectFree == type)
		{
			Fvector xyz;
			XFORM().getXYZi(xyz);
			active_camera->yaw = xyz.y;
		}
	}
}
