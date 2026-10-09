// Embedded so the native Join/settings integration does not require replacing packed Anomaly scripts.
R"COOPLUA(
if not coopnet_options then
    coopnet_options = { values = {}, revision = 0, backups = {} }
    local C = coopnet_options
    function C.world(id)
        return coopnet_world_rule_path(id)
    end
    function C.locked(id)
        return coopnet_world_settings_locked() and C.world(id)
    end
    function C.install()
        if C.installed then return end
        local U = ui_options
        if not U.options or #U.options == 0 then U.init_opt_base() end
        local config = axr_main.config
        local read, write = config.r_value, config.w_value
        config.r_value = function(self, section, id, ...)
            if section == "options" and C.locked(id) and C.values[id] ~= nil then return C.values[id] end
            return read(self, section, id, ...)
        end
        config.w_value = function(self, section, id, value, ...)
            if section == "options" and C.locked(id) then return end
            return write(self, section, id, value, ...)
        end
        local get, set = U.get, U.set
        U.get = function(id)
            if C.locked(id) and C.values[id] ~= nil then return C.values[id] end
            return get(id)
        end
        U.set = function(id, value)
            if C.locked(id) then return end
            return set(id, value)
        end
        local current = U.UIOptions.GetCurrentValue
        U.UIOptions.GetCurrentValue = function(self, path, opt, definition)
            local id = path .. "/" .. opt
            if C.locked(id) and C.values[id] ~= nil then return C.values[id] end
            return current(self, path, opt, definition)
        end
        local cache = U.UIOptions.CacheValue
        U.UIOptions.CacheValue = function(self, path, opt, value, definition)
            if C.locked(path .. "/" .. opt) then return end
            return cache(self, path, opt, value, definition)
        end
        local accept = U.UIOptions.On_Accept
        local pending
        for index = 1, 32 do
            local name, value = debug.getupvalue(accept, index)
            if not name then break end
            if name == "opt_temp" then pending = value; break end
        end
        U.UIOptions.On_Accept = function(self)
            if coopnet_world_settings_locked() and pending then
                local remove = {}
                for id in pairs(pending) do if C.world(id) then remove[#remove + 1] = id end end
                for _, id in ipairs(remove) do pending[id] = nil end
            end
            return accept(self)
        end
        for _, method in ipairs({"Register_Check", "Register_List", "Register_Input", "Register_Track", "Register_Radio", "Register_Button", "Register_BtnAll"}) do
            local original = U.UIOptions[method]
            U.UIOptions[method] = function(self, xml, handler, path, opt, definition, ...)
                local height = original(self, xml, handler, path, opt, definition, ...)
                if C.locked(path .. "/" .. opt) then
                    handler:Enable(false)
                    local caption = self._Cap[string.gsub(path .. "/" .. opt, "/", "_")]
                    if caption then caption:TextControl():SetTextColor(GetARGB(255,150,150,150)) end
                end
                return height
            end
        end
        local reset = U.UIOptions.Reset_opt
        U.UIOptions.Reset_opt = function(self, tree, path, flags)
            local result = reset(self, tree, path, flags)
            self.preset:Enable(not C.locked(path .. "/_preset"))
            self.preset_cap:TextControl():SetTextColor(C.locked(path .. "/_preset") and GetARGB(255,150,150,150) or GetARGB(255,255,255,255))
            return result
        end
        C.installed = true
    end
    function C.capture()
        C.install()
        local rules = {}
        local function visit(tree, path)
            for _, definition in ipairs(tree) do
                local id = path == "" and definition.id or path .. "/" .. definition.id
                if definition.gr then visit(definition.gr, id)
                elseif definition.val ~= nil and C.world(id) then
                    if definition.cmd then coopnet_register_world_command(definition.cmd) end
                    local value
                    if definition.curr then value = ui_options.exec(unpack(definition.curr))
                    elseif definition.cmd then
                        value = get_console_cmd(definition.val == 1 and 1 or 0, definition.cmd)
                        if definition.val == 2 then value = tonumber(value) end
                    else value = ui_options.get(id) end
                    if type(value) == "number" or type(value) == "string" or type(value) == "boolean" then
                        rules[#rules + 1] = { name = id, value = value }
                    end
                end
            end
        end
        visit(ui_options.options, "")
        table.sort(rules, function(a,b) return a.name < b.name end)
        return rules
    end
    function C.apply(revision, rules)
        C.install()
        if C.revision == revision then return true end
        local values = {}
        for _, rule in ipairs(rules) do
            if not C.world(rule.name) or not ui_options.check_opt_table(rule.name) then return false end
            local definition = ui_options.get_opt_table(rule.name)
            if definition.val == nil then return false end
        end
        for _, rule in ipairs(rules) do
            local definition = ui_options.get_opt_table(rule.name)
            if definition.cmd then
                coopnet_register_world_command(definition.cmd)
                if not C.backups[definition.cmd] then C.backups[definition.cmd] = get_console_cmd(0, definition.cmd) end
                local value = rule.value
                if type(value) == "boolean" then value = definition.bool_to_num and (value and "1" or "0") or (value and "on" or "off") end
                get_console():execute(definition.cmd .. " " .. tostring(value))
            end
            values[rule.name] = rule.value
        end
        C.values = values
        C.revision = revision
        return true
    end
    function C.clear()
        for command, value in pairs(C.backups) do get_console():execute(command .. " " .. tostring(value)) end
        C.values = {}; C.backups = {}; C.revision = 0
    end
    function C.probe()
        for id,value in pairs(C.values) do
            if id == "alife/general/alife_mutant_pop" then
                local changed = type(value) == "boolean" and not value or (type(value) == "number" and value + 1 or "coop_probe")
                ui_options.set(id, changed)
                axr_main.config:w_value("options", id, changed)
                assert(ui_options.get(id) == value, "guest changed a host world rule")
                return true
            end
        end
        return false
    end
end
coopnet_options.install()
)COOPLUA"
