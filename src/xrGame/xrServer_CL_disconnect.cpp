#include "stdafx.h"
#include "xrserver.h"
#include "game_sv_single.h"
#include "alife_simulator.h"
#include "xrserver_objects.h"
#include "level.h"

void xrServer::OnCL_Disconnected(IClient* CL)
{
	//csPlayers.Enter			();

	// Game config (all, info includes deleted player now, excludes at the next cl-update)
	NET_Packet P;
	P.B.count = 0;
	P.w_clientID(CL->ID);
	xrClientData* xrCData = (xrClientData*)(CL);
	VERIFY(xrCData);

	if (!xrCData->ps)
		return;

	P.w_stringZ(xrCData->ps->getName());
	P.w_u16(xrCData->ps->GameID);
	P.r_pos = 0;

	ClientID clientID;
	clientID.set(0);

	game->AddDelayedEvent(P, GAME_EVENT_PLAYER_DISCONNECTED, 0, clientID);

	//
	if (GetClientsCount() > 1 && !CL->flags.bLocal)
	{
		// Migrate entities
		for (u16 id : get_entity_ids_in_spawn_order())
		{
			CSE_Abstract* entity = ID_to_entity(id);
			if (entity && entity->owner == CL) PerformMigration(entity, (xrClientData*)CL,
			                                          SelectBestClientToMigrateTo(entity,TRUE));
		}
	}
	else
	{
		// Destroy children before parents, keeping the actor until last.
		while (!entities.empty())
		{
			const auto ids = get_entity_ids_in_spawn_order();
			for (auto it = ids.rbegin(); it != ids.rend(); ++it)
				if (CSE_Abstract* entity = ID_to_entity(*it))
					entity_Destroy(entity);
		}
	}
	//csPlayers.Leave			();

	Server_Client_Check(CL);
}
