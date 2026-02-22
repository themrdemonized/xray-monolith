////////////////////////////////////////////////////////////////////////////
//	Module 		: vision_client.cpp
//	Created 	: 11.06.2007
//  Modified 	: 11.06.2007
//	Author		: Dmitriy Iassenev
//	Description : vision client
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "vision_client.h"
#include "entity.h"
#include "visual_memory_manager.h"

IC const CEntity& vision_client::object() const
{
	VERIFY(m_object);
	return (*m_object);
}

vision_client::vision_client(CEntity* object, const u32& update_interval) :
	Feel::Vision(object),
	m_object(object)
{
	VERIFY(m_object);

	m_visual = xr_new<CVisualMemoryManager>(this);

	m_state = 0;

	shedule.t_min = update_interval;
	shedule.t_max = shedule.t_min;
	shedule_register();
}

vision_client::~vision_client()
{
	Device.secondary_tasks.wait();
	shedule_unregister();
	xr_delete(m_visual);
}

void vision_client::eye_pp_s01()
{
	Device.Statistic->AI_Vis_Query.Begin();

	Fvector c, k, j;
	float field_of_view, aspect_ratio, near_plane, far_plane;
	camera(c, k, j, field_of_view, aspect_ratio, near_plane, far_plane);

	Fmatrix mProject, mFull, mView;
	mView.build_camera_dir(c, k, j);
	m_position = c;
	mProject.build_projection(field_of_view, aspect_ratio, near_plane, far_plane);
	mFull.mul(mProject, mView);

	feel_vision_query(mFull, c);

	Device.Statistic->AI_Vis_Query.End();
}

void vision_client::eye_pp_s2()
{
	Device.Statistic->AI_Vis_RayTests.Begin();

	// Snapshot bone data before going multithreaded
	// Safer in order to avoid crash on get_last_local_point_on_mesh or get_new_local_point_on_mesh calls
	VisionSnapshotList snapshots;

	auto& visible_items = feel_visible;
	snapshots.reserve(visible_items.size());

	// Lock list to ensure safety
	xrSRWLockGuard guard(&lock_visible, true);
	for (auto& item : visible_items)
	{
		VisionSnapshotItem snap;
		snap.Object = item.O;
		snap.HasCFORM = item.O->CFORM() != 0;

		if (item.O->Visual())
		{
			item.O->Center(snap.Position);
		}
		else
		{
			snap.Position = item.O->Position();
		}

		// Initial Setup for new objects (added from o_new)
		if (item.bone_id == u16(-1))
		{
			// If it was just added, pick its first valid point right now
			item.cp_LP = item.O->get_new_local_point_on_mesh(item.bone_id);
		}

		// Prepare data for raycasts
		snap.cp_LAST = item.O->get_last_local_point_on_mesh(item.cp_LP, item.bone_id);
		snap.cp_LP = item.O->get_new_local_point_on_mesh(item.bone_id);
		snap.bone_id = item.bone_id;
	
		snapshots.push_back(snap);
	}

	u32 dwTime = Device.dwTimeGlobal;
	u32 dwDT = dwTime - m_time_stamp;
	m_time_stamp = dwTime;

	static DWORD this_thread_id = 0;
	this_thread_id = GetCurrentThreadId();

	Device.secondary_tasks.run([=]()
	{
		if (this_thread_id != GetCurrentThreadId()) { PROF_THREAD("X-Ray PPL Thread") }
		feel_vision_update(m_object, m_position, float(dwDT) / 1000.f, visual().transparency_threshold(), snapshots);
	});

	Device.Statistic->AI_Vis_RayTests.End();
}

float vision_client::shedule_Scale()
{
	return (0.f);
}

void vision_client::shedule_Update(u32 dt)
{
	PROF_EVENT("vision_client::shedule_Update");
	inherited::shedule_Update(dt);

	if (!object().g_Alive())
		return;

	switch (m_state)
	{
	case 0:
		{
			m_state = 1;
			eye_pp_s01();
			break;
		}
	case 1:
		{
			m_state = 0;
			eye_pp_s2();
			break;
		}
	default: NODEFAULT;
	}

	visual().update(float(dt) / 1000.f);
}

shared_str vision_client::shedule_Name() const
{
	string256 temp;
	xr_sprintf(temp, "vision_client[%s]", *object().cName());
	return (temp);
}

bool vision_client::shedule_Needed()
{
	return (true);
}

float vision_client::feel_vision_mtl_transp(CObject* O, u32 element)
{
	return (visual().feel_vision_mtl_transp(O, element));
}

void vision_client::reinit()
{
	visual().reinit();
}

void vision_client::reload(LPCSTR section)
{
	visual().reload(section);
}

void vision_client::remove_links(CObject* object)
{
	visual().remove_links(object);
}
