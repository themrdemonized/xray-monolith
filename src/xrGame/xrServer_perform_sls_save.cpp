#include "stdafx.h"
#include "xrserver.h"
#include "xrmessages.h"
#include "xrserver_objects.h"

void xrServer::SLS_Save(IWriter& fs)
{
	// Generate spawn+update
	NET_Packet P;
	u32 position;
	xrS_entities::iterator I = entities.begin(), E = entities.end();
	for (u32 C = 0; I != E; ++I, ++C)
	{
		CSE_Abstract* E = I->second;

		fs.open_chunk(C);

		// Spawn
		E->Spawn_Write(P,TRUE);
		if (P.overflowed)
			Msg("! object [%s] id %u spawn was cut off at %u bytes", E->name_replace(), E->ID, P.B.count);
		const u32 spawn_bytes = P.B.count > 0xFFFF ? 0xFFFF : P.B.count;
		fs.w_u16((u16)spawn_bytes);
		fs.w(P.B.data, spawn_bytes);

		// Update
		P.w_begin(M_UPDATE);
		P.w_u32(E->ID);
		P.w_chunk_open8(position);
		const bool overflow_before = P.overflowed;
		E->UPDATE_Write(P);
		const u32 update_size = u32(P.w_tell() - position) - sizeof(u8);
		if (update_size > 255 || (P.overflowed && !overflow_before))
		{
			Msg("! update for [%s] id %u is %u bytes and was not saved", E->name_replace(), E->ID, update_size);
			P.B.count = position + sizeof(u8);
			P.overflowed = overflow_before;
			u8 empty = 0;
			P.w_seek(position, &empty, sizeof(empty));
		}
		else
			P.w_chunk_close8(position);

		const u32 update_bytes = P.B.count > 0xFFFF ? 0xFFFF : P.B.count;
		fs.w_u16((u16)update_bytes);
		fs.w(P.B.data, update_bytes);

		fs.close_chunk();
	}
}
