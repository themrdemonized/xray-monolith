#pragma once
#include "object_interfaces.h"
#include "map_location_defs.h"
#include "../xrServerEntities/alife_space.h"

class CMapLocationWrapper;
class CInventoryOwner;
class CMapLocation;

class CMapManager
{
	struct SRemovedSpot
	{
		shared_str spot_type;
		ALife::_OBJECT_ID object_id;
	};

	CMapLocationWrapper* m_locations_wrapper;
	Locations* m_locations;
	xr_vector<CMapLocation*> m_deffered_destroy_queue;

	xr_vector<SRemovedSpot> m_removed_spots;
	void QueueRemovedNotify(const shared_str& spot_type, ALife::_OBJECT_ID id);
public:
	// called from the main thread only
	void FlushRemovedNotify();

	CMapManager();
	~CMapManager();
	void __stdcall Update();
	/*ICF */
	Locations& Locations(); //{return *m_locations;}
	CMapLocation* AddMapLocation(const shared_str& spot_type, ALife::_OBJECT_ID id);
	CMapLocation* AddRelationLocation(CInventoryOwner* pInvOwner);
	void RemoveMapLocation(const shared_str& spot_type, ALife::_OBJECT_ID id);

	// demonized: remove all map object spots by id
	void RemoveAllMapLocationsById(ALife::_OBJECT_ID id);

	bool HasMapLocation(const shared_str& spot_type, ALife::_OBJECT_ID id);
	void RemoveMapLocationByObjectID(ALife::_OBJECT_ID id); //call on destroy object
	void RemoveMapLocation(CMapLocation* ml);
	CMapLocation* GetMapLocation(const shared_str& spot_type, ALife::_OBJECT_ID id);
	void GetMapLocations(const shared_str& spot_type, ALife::_OBJECT_ID id, xr_vector<CMapLocation*>& res);
	void GetMapLocations(ALife::_OBJECT_ID id, xr_vector<CMapLocation*>& res);
	void DisableAllPointers();
	void ReloadSpots();
	bool GetMapLocationsForObject(ALife::_OBJECT_ID id, xr_vector<CMapLocation*>& res);
	void OnObjectDestroyNotify(ALife::_OBJECT_ID id);
	void ResetStorage() { m_locations = NULL; m_removed_spots.clear_not_free(); };
#ifdef DEBUG
	void					Dump						();
#endif
	void Destroy(CMapLocation*);
};
