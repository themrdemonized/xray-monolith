#include "stdafx.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "alife_object_registry.h"
#include "xrMessages.h"

#include <cmath>

// Round-trip coverage for the widened object-id save format.
// Run from the console with `saveload_test`, or start the game with -saveload_test.

namespace
{
	int g_failed = 0;
	int g_passed = 0;

	void check(bool ok, const char* name)
	{
		if (ok)
		{
			++g_passed;
			Msg("* saveload_test ok: %s", name);
		}
		else
		{
			++g_failed;
			Msg("! saveload_test FAILED: %s", name);
		}
	}

	bool near_float(float value, float expected)
	{
		return std::fabs(value - expected) < 0.0001f;
	}

	void test_saved_object_id_lists()
	{
		NET_Packet legacy;
		legacy.write_start();
		legacy.w_u32(3);
		legacy.w_u16(1);
		legacy.w_u16(ALife::invalid_object_id);
		legacy.w_u16(40000);
		const u32 legacy_end = legacy.B.count;
		legacy.r_pos = 0;

		xr_vector<ALife::_OBJECT_ID> loaded;
		load_saved_object_ids(loaded, legacy, SPAWN_VERSION_LEGACY_IDS);
		check(loaded.size() == 3 && loaded[0] == 1 && loaded[1] == ALife::invalid_object_id && loaded[2] == 40000,
		      "legacy restriction ids stay 16-bit");
		check(legacy.r_pos == legacy_end, "legacy restriction reader consumes exactly the old record");

		NET_Packet current;
		current.write_start();
		current.w_u32(3);
		current.w_u32(65536);
		current.w_u32(ALife::invalid_object_id);
		current.w_u32(ALife::max_object_id);
		const u32 current_end = current.B.count;
		current.r_pos = 0;
		loaded.clear();
		load_saved_object_ids(loaded, current, SPAWN_VERSION);
		check(loaded.size() == 3 && loaded[0] == 65536 && loaded[1] == ALife::invalid_object_id &&
		          loaded[2] == ALife::max_object_id,
		      "current restriction ids keep values above 65535");
		check(current.r_pos == current_end, "current restriction reader consumes the whole record");
	}

	void test_update_and_event_headers()
	{
		NET_Packet update;
		update.write_start();
		const u32 id = 65536;
		const u8 payload[] = {9, 8, 7, 6};
		update.w_u32(id);
		update.w_u8(sizeof(payload));
		update.w(payload, sizeof(payload));
		update.r_pos = 0;

		u32 read_id = 0;
		u8 size = 0;
		update.r_u32(read_id);
		update.r_u8(size);
		u8 bytes[4] = {};
		update.r(bytes, size);
		check(read_id == id && size == sizeof(payload) && bytes[0] == 9 && bytes[3] == 6,
		      "update record id is 32-bit");

		NET_Packet event;
		event.write_start();
		event.w_u32(4321);
		event.w_u16(GE_DESTROY);
		event.w_u32(65623);
		event.r_pos = 0;
		u32 time = 0;
		u16 type = 0;
		u32 dest = 0;
		event.r_u32(time);
		event.r_u16(type);
		event.r_u32(dest);
		check(time == 4321 && type == GE_DESTROY && dest == 65623, "destroy event destination is 32-bit");
	}

	void test_packet_limit()
	{
		NET_Packet packet;
		packet.write_start();
		const u8 byte = 1;
		while (packet.B.count + 1 < NET_PacketSizeLimit)
			packet.w_u8(byte);
		const u32 filled = packet.B.count;
		packet.w_u8(byte);
		check(filled + 1 == NET_PacketSizeLimit && packet.B.count == filled,
		      "a write past the packet buffer is refused");
		check(NET_PacketSizeLimit > 16000 && NET_PacketSizeLimit <= 65535,
		      "packet limit can hold a large actor and still fits a u16 size");
	}

	bool read_spawn_ids(NET_Packet& packet, u16& legacy_id, u16& legacy_parent, u16& legacy_phantom, u16& version,
	                    u32& id, u32& parent, u32& phantom)
	{
		packet.r_pos = 0;
		u16 message = 0;
		packet.r_begin(message);
		if (message != M_SPAWN)
			return false;
		shared_str name;
		shared_str replace;
		packet.r_stringZ(name);
		packet.r_stringZ(replace);
		packet.r_u8();
		packet.r_u8();
		Fvector unused;
		packet.r_vec3(unused);
		packet.r_vec3(unused);
		packet.r_u16();
		packet.r_u16(legacy_id);
		packet.r_u16(legacy_parent);
		packet.r_u16(legacy_phantom);
		packet.r_u16();
		packet.r_u16(version);
		if (version < SPAWN_VERSION)
			return true;
		packet.r_u32(id);
		packet.r_u32(parent);
		packet.r_u32(phantom);
		return true;
	}

	void test_entity_round_trip(LPCSTR section, ALife::_OBJECT_ID id, ALife::_OBJECT_ID parent, ALife::_OBJECT_ID phantom,
	                            bool actor_fields)
	{
		CSE_Abstract* source = F_entity_Create(section);
		CSE_Abstract* destination = F_entity_Create(section);
		if (!source || !destination)
		{
			check(false, section);
			F_entity_Destroy(source);
			F_entity_Destroy(destination);
			return;
		}

		source->ID = id;
		source->ID_Parent = parent;
		source->ID_Phantom = phantom;
		auto* source_creature = source->cast_creature_abstract();
		if (actor_fields && source_creature)
		{
			source_creature->set_health(0.75f);
			source_creature->set_killer_id(ALife::invalid_object_id);
			source_creature->m_dynamic_out_restrictions.clear();
			source_creature->m_dynamic_out_restrictions.push_back(65536);
			source_creature->m_dynamic_out_restrictions.push_back(ALife::invalid_object_id);
			source_creature->m_dynamic_out_restrictions.push_back(ALife::max_object_id);
			source_creature->m_dynamic_in_restrictions.clear();
			source_creature->m_dynamic_in_restrictions.push_back(42);
		}
		auto* source_actor = smart_cast<CSE_ALifeCreatureActor*>(source);
		if (source_actor)
			source_actor->m_holderID = 70001;

		NET_Packet spawn;
		source->Spawn_Write(spawn, TRUE);
		check(spawn.B.count > 0 && spawn.B.count < NET_PacketSizeLimit, "spawn record fits the packet buffer");
		check(spawn.B.count <= 65535, "spawn record fits the u16 size prefix");

		u16 legacy_id = 1;
		u16 legacy_parent = 1;
		u16 legacy_phantom = 1;
		u16 version = 0;
		u32 wide_id = 0;
		u32 wide_parent = 0;
		u32 wide_phantom = 0;
		const bool header = read_spawn_ids(spawn, legacy_id, legacy_parent, legacy_phantom, version, wide_id, wide_parent,
		                                   wide_phantom);
		check(header && version == SPAWN_VERSION_WIDE_REFS, "spawn version is the wide-id version");
		check(wide_id == id && wide_parent == parent && wide_phantom == phantom, "wide spawn ids match");
		check(legacy_id == (id <= ALife::invalid_object_id ? u16(id) : 0), "legacy id slot is zero when the id does not fit");
		check(legacy_parent == u16(parent) && legacy_phantom == u16(phantom), "legacy parent and phantom slots");

		NET_Packet spawn_copy;
		spawn_copy.write_start();
		spawn_copy.w(spawn.B.data, spawn.B.count);
		spawn_copy.r_pos = 0;
		destination->Spawn_Read(spawn_copy);
		check(destination->ID == id && destination->ID_Parent == parent && destination->ID_Phantom == phantom,
		      "spawn read restores 32-bit ids");

		NET_Packet update;
		update.w_begin(M_UPDATE);
		source->UPDATE_Write(update);

		NET_Packet record;
		record.write_start();
		record.w_u16(u16(spawn.B.count));
		record.w(spawn.B.data, spawn.B.count);
		record.w_u16(u16(update.B.count));
		record.w(update.B.data, update.B.count);

		IReader reader(record.B.data, (int)record.B.count);
		CSE_ALifeDynamicObject* stored = CALifeObjectRegistry::get_object(reader);
		check(stored && stored->ID == id && stored->ID_Parent == parent, "registry spawn record reloads the same ids");
		auto* stored_creature = stored ? stored->cast_creature_abstract() : nullptr;
		if (actor_fields && stored_creature)
		{
			const auto& out_ids = stored_creature->m_dynamic_out_restrictions;
			auto* stored_actor = smart_cast<CSE_ALifeCreatureActor*>(stored);
			check(stored_creature->g_Alive() && near_float(stored_creature->get_health(), 0.75f),
			      "reloaded actor stays alive at the saved health");
			check(stored_creature->get_killer_id() == ALife::invalid_object_id, "reloaded actor killer stays the empty id");
			check(out_ids.size() == 3 && out_ids[0] == 65536 && out_ids[1] == ALife::invalid_object_id &&
			          out_ids[2] == ALife::max_object_id,
			      "reloaded actor restriction ids survive");
			check(stored_actor && stored_actor->m_holderID == 70001, "reloaded actor holder id is not truncated");
		}

		F_entity_Destroy(stored);
		F_entity_Destroy(source);
		F_entity_Destroy(destination);
	}
}

int run_saveload_cycle_tests()
{
	g_failed = 0;
	g_passed = 0;
	Msg("* saveload_test start");

	check(sizeof(ALife::_OBJECT_ID) == sizeof(u32), "object id type is 32-bit");
	check(ALife::invalid_object_id == 65535 && ALife::max_object_id == 131071, "sentinel and max id");

	test_saved_object_id_lists();
	test_update_and_event_headers();
	test_packet_limit();
	test_entity_round_trip("bolt", 65536, ALife::invalid_object_id, 42, false);
	test_entity_round_trip("actor", 70000, ALife::invalid_object_id, 42, true);

	Msg("* saveload_test %d passed, %d failed", g_passed, g_failed);
	return g_failed ? 1 : 0;
}

class CCC_SaveLoadCycleTest : public IConsole_Command
{
public:
	CCC_SaveLoadCycleTest(LPCSTR name) : IConsole_Command(name) { bEmptyArgsHandled = true; }

	virtual void Execute(LPCSTR) override
	{
		run_saveload_cycle_tests();
	}
};

void register_saveload_cycle_test_command()
{
	CMD1(CCC_SaveLoadCycleTest, "saveload_test");
}
