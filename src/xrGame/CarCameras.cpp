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
#include "../Include/xrRender/Kinematics.h"

// Third-person cameras orbit the middle of the car, slightly above the roof line, instead of the driver's
// head (camera_pos), so the view is symmetric. car_definition "chase_camera_pivot" (object space) overrides it.
Fvector CCar::ChaseCameraPivot()
{
	Fvector P;
	if (!m_vho.enabled)
	{
		// stock: orbit the driver's head
		XFORM().transform_tiny(P, m_camera_position);
		return P;
	}
	CInifile* ini = smart_cast<IKinematics*>(Visual())->LL_UserData();
	if (ini && ini->line_exist("car_definition", "chase_camera_pivot"))
	{
		XFORM().transform_tiny(P, ini->r_fvector3("car_definition", "chase_camera_pivot"));
		return P;
	}
	Fvector local;
	if (m_vho.s.chase_pivot_ok)
		local = m_vho.s.chase_pivot;
	else
	{
		const Fbox& box = BoundingBox();
		box.getcenter(local);
		local.y = box.min.y + (box.max.y - box.min.y) * 0.8f;
	}
	local.y += m_vho.p.camera_raise;
	XFORM().transform_tiny(P, local);
	return P;
}

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
	// the car camera's collision fix only runs with the handling overhaul; otherwise it is the stock camera
	static_cast<CCameraCarLook*>(camera[ectChase])->m_overhaul = m_vho.enabled;
	static_cast<CCameraCarLook*>(camera[ectFree])->m_overhaul = m_vho.enabled;

#ifdef CAR_NEW
    u16 bone_id = GetCameraBone();
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
			P = ChaseCameraPivot();
		}
		break;
		}

		cam->f_fov = fov / GetZoomFactor();
		cam->Update(P, D);
		Level().Cameras().UpdateFromCamera(cam);
		return;
	}
#endif

	if (active_camera->tag == ectFirst)
	{
		Fvector eye = m_camera_position;
		if (m_vho.enabled) eye.y += m_vho.p.camera_raise; // handling overhaul: per-vehicle seat height tweak
		XFORM().transform_tiny(P, eye);
	}
	else
		P = ChaseCameraPivot();

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
