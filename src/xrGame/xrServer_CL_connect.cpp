#include "stdafx.h"
#include "xrserver.h"
#include "xrmessages.h"
#include "xrserver_objects.h"
#include "xrServer_Objects_Alife_Monsters.h"
#include "Level.h"

extern ENGINE_API bool g_dedicated_server;

void xrServer::Prepare_connect_spawn(CSE_Abstract* E, xr_vector<PreparedClientSpawn>& prepared)
{
	xr_vector<u16>::iterator it = std::find(conn_spawned_ids.begin(), conn_spawned_ids.end(), E->ID);
	if (it != conn_spawned_ids.end())
		return;

	conn_spawned_ids.push_back(E->ID);

	if (E->net_Processed) return;
	if (E->s_flags.is(M_SPAWN_OBJECT_PHANTOM)) return;

	CSE_Abstract* Parent = ID_to_entity(E->ID_Parent);
	if (Parent)
		Prepare_connect_spawn(Parent, prepared);

	PreparedClientSpawn spawn = {};
	spawn.entity = E;
	spawn.section = E->s_name;
	spawn.id = E->ID;
	spawn.parent_id = E->ID_Parent;
	if (CSE_Visual* visual = E->visual())
		spawn.actual_visual = visual->get_visual();
	if (pSettings->section_exist(E->s_name.c_str()) && pSettings->line_exist(E->s_name.c_str(), "visual"))
		spawn.ltx_visual = pSettings->r_string(E->s_name.c_str(), "visual");
	prepared.push_back(std::move(spawn));
}

void xrServer::Perform_connect_spawn(const PreparedClientSpawn& prepared, xrClientData* CL, NET_Packet& P)
{
	CSE_Abstract* E = prepared.entity;
	P.B.count = 0;

	Flags16 save = E->s_flags;
	//-------------------------------------------------
	E->s_flags.set(M_SPAWN_UPDATE,TRUE);
	if (0 == E->owner)
	{
		// PROCESS NAME; Name this entity
		if (E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER))
		{
			CL->owner = E;
			VERIFY(CL->ps);
			E->set_name_replace(CL->ps->getName());
		}

		// Associate
		E->owner = CL;
		E->Spawn_Write(P,TRUE);
		E->UPDATE_Write(P);

		CSE_ALifeObject* object = smart_cast<CSE_ALifeObject*>(E);
		VERIFY(object);
		if (!object->keep_saved_data_anyway())
			object->client_data.clear();
	}
	else
	{
		E->Spawn_Write(P, FALSE);
		E->UPDATE_Write(P);
		//		CSE_ALifeObject*	object = smart_cast<CSE_ALifeObject*>(E);
		//		VERIFY				(object);
		//		VERIFY				(object->client_data.empty());
	}
	//-----------------------------------------------------
	E->s_flags = save;
	SendTo(CL->ID, P, net_flags(TRUE,TRUE));
	E->net_Processed = TRUE;
}

void xrServer::SendConfigFinished(ClientID const& clientId)
{
	NET_Packet P;
	P.w_begin(M_SV_CONFIG_FINISHED);
	SendTo(clientId, P, net_flags(TRUE,TRUE));
}

void xrServer::SendConnectionData(IClient* _CL)
{
	conn_spawned_ids.clear();
	xrClientData* CL = (xrClientData*)_CL;
	NET_Packet P;
	xr_vector<PreparedClientSpawn> prepared;
	prepared.reserve(entities.size());
	xrS_entities::iterator I = entities.begin(), E = entities.end();
	for (; I != E; ++I) I->second->net_Processed = FALSE;
	for (I = entities.begin(); I != E; ++I)
		Prepare_connect_spawn(I->second, prepared);
	NativeLoadExecutor& executor = NativeLoadExecutor::Instance();
	string_path resolved_level_path;
	FS.update_path(resolved_level_path, "$level$", "");
	const xr_string level_path = resolved_level_path;
	const bool prepare_local_resources = !g_dedicated_server && CL == GetServerClient();
	try
	{
		if (prepare_local_resources)
		for (PreparedClientSpawn& spawn : prepared)
		{
			spawn.resource_batch = executor.BeginBatch(executor.CurrentGeneration());
			auto prepare_resources = [&spawn, level_path]()
			{
				if (spawn.actual_visual.size())
					::Render->model_CollectTextures(spawn.actual_visual.c_str(), level_path.c_str(), spawn.textures);
				if (spawn.ltx_visual.size() && spawn.ltx_visual != spawn.actual_visual)
					::Render->model_CollectTextures(spawn.ltx_visual.c_str(), level_path.c_str(), spawn.textures);
				std::sort(spawn.textures.begin(), spawn.textures.end());
				spawn.textures.erase(std::unique(spawn.textures.begin(), spawn.textures.end()), spawn.textures.end());
				for (const xr_string& texture : spawn.textures)
					Device.m_pRender->ResourcesPrefetchCreateTexture(texture.c_str(), level_path.c_str());
			};
			if (spawn.resource_batch.Valid())
				executor.Submit(spawn.resource_batch, NativeLoadPriority::Spawn, std::move(prepare_resources));
			else
				prepare_resources();
		}

		u32 order_hash = 0;
		u32 texture_count = 0;
		u32 model_count = 0;
		for (const PreparedClientSpawn& spawn : prepared)
		{
			if (prepare_local_resources && spawn.resource_batch.Valid())
				executor.Wait(spawn.resource_batch);
			#ifdef SPAWN_ANTIFREEZE
			if (prepare_local_resources)
				Level().RegisterPreparedClientSpawnResource(spawn.id, spawn.parent_id, spawn.section,
					spawn.actual_visual, spawn.ltx_visual, level_path.c_str());
			#endif
			model_count += spawn.actual_visual.size() || spawn.ltx_visual.size() ? 1u : 0u;
			texture_count += static_cast<u32>(spawn.textures.size());
			order_hash = crc32(&spawn.id, sizeof(spawn.id), order_hash);
			order_hash = crc32(&spawn.parent_id, sizeof(spawn.parent_id), order_hash);
			order_hash = crc32(spawn.section.c_str(), xr_strlen(spawn.section.c_str()), order_hash);
			Perform_connect_spawn(spawn, CL, P);
		}
		Msg("* [client-spawn] prepared=%u models=%u textures=%u order_hash=%08x", static_cast<u32>(prepared.size()),
			model_count, texture_count, order_hash);
	}
	catch (...)
	{
		const std::exception_ptr failure = std::current_exception();
		// Submitted workers keep references to vector elements. Drain every batch
		// before the vector can unwind, even when one task failed first.
		for (const PreparedClientSpawn& spawn : prepared)
			if (spawn.resource_batch.Valid())
				try { executor.Wait(spawn.resource_batch); } catch (...) {}
		std::rethrow_exception(failure);
	}

	// Start to send server logo and rules
	SendServerInfoToClient(CL->ID);

	/*
		Msg("--- Our sended SPAWN IDs:");
		xr_vector<u16>::iterator it = conn_spawned_ids.begin();
		for (; it != conn_spawned_ids.end(); ++it)
		{
			Msg("%d", *it);
		}
		Msg("---- Our sended SPAWN END");
	*/
};

void xrServer::OnCL_Connected(IClient* _CL)
{
	xrClientData* CL = (xrClientData*)_CL;
	CL->net_Accepted = TRUE;
	/*if (Level().IsDemoPlay())
	{
		Level().StartPlayDemo();
		return;
	};*/
	///	Server_Client_Check(CL);
	//csPlayers.Enter					();	//sychronized by a parent call
	Export_game_type(CL);
	Perform_game_export();
	SendConnectionData(CL);

	VERIFY2(CL->ps, "Player state not created");
	if (!CL->ps)
	{
		Msg("! ERROR: Player state not created - incorect message sequence!");
		return;
	}

	game->OnPlayerConnect(CL->ID);
}

void xrServer::SendConnectResult(IClient* CL, u8 res, u8 res1, char* ResultStr)
{
	NET_Packet P;
	P.w_begin(M_CLIENT_CONNECT_RESULT);
	P.w_u8(res);
	P.w_u8(res1);
	P.w_stringZ(ResultStr);
	P.w_clientID(CL->ID);

	if (SV_Client && SV_Client == CL)
		P.w_u8(1);
	else
		P.w_u8(0);
	P.w_stringZ(Level().m_caServerOptions);

	SendTo(CL->ID, P);

	if (!res) //need disconnect 
	{
#ifdef MP_LOGGING
		Msg("* Server disconnecting client, resaon: %s", ResultStr);
#endif
		Flush_Clients_Buffers();
		DisconnectClient(CL, ResultStr);
	}

	if (Level().IsDemoPlay())
	{
		Level().StartPlayDemo();

		return;
	}
};

void xrServer::SendProfileCreationError(IClient* CL, char const* reason)
{
	VERIFY(CL);

	NET_Packet P;
	P.w_begin(M_CLIENT_CONNECT_RESULT);
	P.w_u8(0);
	P.w_u8(ecr_profile_error);
	P.w_stringZ(reason);
	P.w_clientID(CL->ID);
	SendTo(CL->ID, P);
	if (CL != GetServerClient())
	{
		Flush_Clients_Buffers();
		DisconnectClient(CL, reason);
	}
}

//this method response for client validation on connect state (CLevel::net_start_client2)
//the first validation is CDKEY, then gamedata checksum (NeedToCheckClient_BuildVersion), then 
//banned or not...
//WARNING ! if you will change this method see M_AUTH_CHALLENGE event handler
void xrServer::Check_GameSpy_CDKey_Success(IClient* CL)
{
	if (NeedToCheckClient_BuildVersion(CL))
		return;
	//-------------------------------------------------------------
	RequestClientDigest(CL);
};

BOOL g_SV_Disable_Auth_Check = FALSE;

bool xrServer::NeedToCheckClient_BuildVersion(IClient* CL)
{
	/*#ifdef DEBUG
	
		return false; 
	
	#endif*/
	xrClientData* tmp_client = smart_cast<xrClientData*>(CL);
	VERIFY(tmp_client);
	PerformSecretKeysSync(tmp_client);


	if (g_SV_Disable_Auth_Check) return false;
	CL->flags.bVerified = FALSE;
	NET_Packet P;
	P.w_begin(M_AUTH_CHALLENGE);
	SendTo(CL->ID, P);
	return true;
};

void xrServer::OnBuildVersionRespond(IClient* CL, NET_Packet& P)
{
	u16 Type;
	P.r_begin(Type);
	u64 _our = FS.auth_get();
	u64 _him = P.r_u64();

#ifdef USE_DEBUG_AUTH
	Msg("_our = %d", _our);
	Msg("_him = %d", _him);
	_our = MP_DEBUG_AUTH;
#endif // USE_DEBUG_AUTH

	if (_our != _him)
	{
		SendConnectResult(CL, 0, ecr_data_verification_failed, "Data verification failed. Cheater?");
	}
	else
	{
		bool bAccessUser = false;
		string512 res_check;

		if (!CL->flags.bLocal)
		{
			bAccessUser = Check_ServerAccess(CL, res_check);
		}

		if (CL->flags.bLocal || bAccessUser)
		{
			//Check_BuildVersion_Success( CL );
			RequestClientDigest(CL);
		}
		else
		{
			Msg("* Client 0x%08x has an incorrect password", CL->ID.value());
			xr_strcat(res_check, "Invalid password.");
			SendConnectResult(CL, 0, ecr_password_verification_failed, res_check);
		}
	}
};

void xrServer::Check_BuildVersion_Success(IClient* CL)
{
	CL->flags.bVerified = TRUE;
	SendConnectResult(CL, 1, 0, "All Ok");
};
