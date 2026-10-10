// Anomaly 1.5.3 combat evaluation, with every actor treated as a player.
R"COOPLUA(
function is_enemy(obj,enemy,no_memory)
	--utils_data.debug_write("eval_is_enemy")
	if(device().precache_frame > 1) then
		return false -- fix actor dying before game loads from enemies. Also fixes strange crash on game load when in combat sometimes.
	end
	
	if (obj:clsid() == clsid.crow) or (enemy:clsid() == clsid.crow) then -- freakin crows!
		return false 
	end
	
	if not (obj:alive()) then 
		return false 
	end
	
	if not (enemy:alive()) then 
		return false 
	end
	
	if (obj:critically_wounded()) then 
		return true 
	end
	
	local flags = {override = false, result = false}
	SendScriptCallback("on_enemy_eval",obj,enemy,flags)
	
	if flags.override then return flags.result end
	
	local ene_id = enemy:id()
	local id = obj:id()
	local st = db.storage[id]
	
	if (DEV_DEBUG) then
		if (xrs_debug_tools and xrs_debug_tools.debug_invis and utils_obj.is_actor(enemy)) then
			return false
		end
	end
	
	-- xr_bribe							   
	if xr_bribe and utils_obj.is_actor(enemy) then
		if xr_bribe.at_peace(character_community(obj),character_community(enemy),enemy:position():distance_to_sqr(obj:position())) then				 
			return false
		end
	end
	
	local obj_is_stalker = IsStalker(obj)
	-- Ignore long unseen/unheard enemies
	if (obj_is_stalker) then
		if (no_memory ~= true) then
			local tg = time_global()
			local time_in_memory = tg - obj:memory_time(enemy)
			if (time_in_memory < 0) then
				--obj:enable_memory_object(enemy,false)
				--time_in_memory = time_in_memory + load_var(obj,"mem_time_offset",0)
				if (utils_obj.is_actor(enemy) and time_in_memory < -120000 or time_in_memory < -60000) then 
					return false 
				end
			else 
				if (utils_obj.is_actor(enemy) and time_in_memory > 120000 or time_in_memory > 60000) then
					--obj:enable_memory_object(enemy,false)
					--save_var(obj,"mem_time_offset",tg)
					return false
				end
			end
		end
	end

	-- NPC was hit by actor always return true. It's for keep_when_attacked condition
	if (utils_obj.is_actor(enemy) and load_var(obj,"xr_combat_ignore_enabled",true) == false) then
		st.enemy_id = ene_id
		return true
	end

	if (obj:has_info("npcx_is_companion") or enemy:has_info("npcx_is_companion")) then
		-- If companion is in stealth mode then ignore combat greater than 30m unless enemy is in combat with actor
		if (obj:has_info("npcx_beh_substate_stealth") or enemy:has_info("npcx_beh_substate_stealth")) then 
			if not (fighting_with_actor_npcs[id] or fighting_with_actor_npcs[ene_id]) then 
				if (enemy:position():distance_to_sqr(obj:position()) < 900) then 
					return false
				end
			end
		end
	else
		
		local is_actor = (utils_obj.is_actor(enemy))
		
		-- Ignore enemies during surge, only if npc has a surge job at a smart_terrain and the enemy is greater then 25m (actor 100m) away from the npc's surge job's alife_task position
		if (xr_conditions.surge_started) then 
			local smart = xr_gulag.get_npc_smart(obj)
			local npc_info = smart and smart.npc_info and smart.npc_info[id]
			if (npc_info) then
				local npc_job = npc_info.job
				if (npc_job and npc_job.job_type_id == 2) then
					local alife_task = npc_job.alife_task
					if (alife_task) then 
						if (is_actor and enemy:position():distance_to_sqr(alife_task:position()) > 10000) then 
							return false 
						elseif (enemy:position():distance_to_sqr(alife_task:position()) > 625) then
							return false
						end
					end
				end
			end
		end
		
		local sim = alife()
		local is_monster = character_community(enemy) == "zombied" or IsMonster(enemy)
		
		-- Ignore enemies with the safe_zone flag
		if (is_actor ~= true) then
		
			-- ignore hostages
			if (axr_task_manager.hostages_by_id[ene_id]) then
				return true 
			end
			
			if (obj_is_stalker and not is_monster) then
				local se_obj = sim:object(id)
				if not (se_obj) then 
					return false
				end
				
				local cid = se_obj.group_id and se_obj.group_id ~= 65535 and se_obj.group_id or id
				local tg = time_global()
				
				if (safe_zone_npcs[cid]) then
					db.storage[id].heli_enemy_flag = nil
					if (tg < safe_zone_npcs[cid] + 8000) then
						return false
					end
					safe_zone_npcs[cid] = nil
				else
					for i,v in ipairs(ignored_zone) do
						if (utils_obj.npc_in_zone(obj, v)) then
							safe_zone_npcs[cid] = tg
							return false
						end
					end
				end
			
				local ene_squad = get_object_squad(enemy)
				local bid = ene_squad and ene_squad.id or ene_id
				if (safe_zone_npcs[bid]) then
					return false
				else 
					for i,v in ipairs(ignored_zone) do
						if (utils_obj.npc_in_zone(enemy, v)) then
							safe_zone_npcs[bid] = tg
							return false
						end
					end
				end
			end
		end

		--Ignore by distance; mainly unrestricted for all non-story stalker vs. stalker combat
		local pos1 = obj:position()
		local pos2 = enemy:position()
		
		local comm = character_community(obj)
		local ene_comm = character_community(enemy)
		if (comm == "army" and ene_comm == "stalker") or (comm == "stalker" and comm == "army") then 
			-- In Cordon prevent stalker v military combat with distances greater than 60m
			if (level.name() == "l01_escape" and pos1:distance_to_sqr(pos2) >= 3600) then
				return false
			end
		end

		if (is_actor) then
			if (level.get_time_hours() < 4 or level.get_time_hours() > 19 or level.rain_factor() >= 0.4) and (pos1:distance_to_sqr(pos2) > 10000) then 
				-- limit combat range to 100m during night hours or heavy rain for stalker v actor
				return false
			end
		elseif (is_monster) then
			if (math.abs(pos1.y-pos2.y) > 30) then
				-- prevent stalker from attack mutants under ground or above ground
				return false 
			end
			if (pos1:distance_to_sqr(pos2) > 4900) then
				-- ignore monsters 70m or greater
				return false
			end
		else
			local dist = pos1:distance_to_sqr(pos2)
			if (dist > 10000) then -- 100m max combat range for stalker v stalker
				return false
			end
			if (dist > 5625) and (level.rain_factor() > 0 or level.get_time_hours() < 4 or level.get_time_hours() > 19) then 
				-- limit combat range to 75m during night hours for stalker v stalker or rain
				return false
			end
			if (dist > 3600) then
				-- limit combat range to 60m during heavy rain, surges or if they are story related
				if (xr_conditions.surge_started() or level.rain_factor() >= 0.5 or get_object_story_id(id) or get_object_story_id(ene_id)) then 
					return false
				end
				
				local squad = get_object_squad(obj)
				local ene_squad = get_object_squad(enemy)
				
				if (squad and get_object_story_id(squad.id) or ene_squad and get_object_story_id(ene_squad.id)) then 
					return false
				end
			end
		end
	end

	-- Store Pure enemy (Enemy without overrides)
	if (obj:relation(enemy) >= game_object.enemy) then
		st.enemy_id = ene_id
	end
	
	-- Ignore enemies by overrides
	if (ignore_enemy_by_overrides(obj,enemy)) then
		return false
	end

	return true
end


)COOPLUA"

