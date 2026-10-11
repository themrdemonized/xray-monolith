#pragma once
#include "object_interfaces.h"
#include "map_location_defs.h"
#include "script_export_space.h"

class CMapLocationWrapper;
class CInventoryOwner;
class CMapLocation;

class CMapManager
{
	struct SRemovedSpot
	{
		shared_str spot_type;
		u16 object_id;
	};

	CMapLocationWrapper* m_locations_wrapper;
	Locations* m_locations;
	xr_vector<CMapLocation*> m_deffered_destroy_queue;

	xr_vector<SRemovedSpot> m_removed_spots;
	void QueueRemovedNotify(const shared_str& spot_type, u16 id);
public:
	// called from the main thread only
	void FlushRemovedNotify();

	CMapManager();
	~CMapManager();
	void __stdcall Update();
	/*ICF */
	Locations& Locations(); //{return *m_locations;}
	CMapLocation* AddMapLocation(const shared_str& spot_type, u16 id);
	CMapLocation* AddRelationLocation(CInventoryOwner* pInvOwner);
	void RemoveRelationLocation(CInventoryOwner* pInvOwner);
	void RemoveMapLocation(const shared_str& spot_type, u16 id);

	// demonized: remove all map object spots by id
	void RemoveAllMapLocationsById(u16 id);

	bool HasMapLocation(const shared_str& spot_type, u16 id);
	void RemoveMapLocationByObjectID(u16 id); //call on destroy object
	void RemoveMapLocation(CMapLocation* ml);
	CMapLocation* GetMapLocation(const shared_str& spot_type, u16 id);
	void GetMapLocations(const shared_str& spot_type, u16 id, xr_vector<CMapLocation*>& res);
	void GetMapLocations(u16 id, xr_vector<CMapLocation*>& res);
	void DisableAllPointers();
	void ReloadSpots();
	bool GetMapLocationsForObject(u16 id, xr_vector<CMapLocation*>& res);
	void OnObjectDestroyNotify(u16 id);
	void ResetStorage() { m_locations = NULL; m_removed_spots.clear_not_free(); };
#ifdef DEBUG
	void					Dump						();
#endif
	void Destroy(CMapLocation*);

	DECLARE_SCRIPT_REGISTER_FUNCTION
};
