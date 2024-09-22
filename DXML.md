# DXML

---

## Intro

DXML by demonized#1084

DXML is a part of [modded exes repo](https://github.com/themrdemonized/STALKER-Anomaly-modded-exes)

DXML allows to manipulate XML files before they loaded into the engine or scripts by utilizing Lua
The engine sends XML string to Lua where it is transformed into DOM-like object (from now on lets call it xml_obj) that can be manipulated by Lua methods and then it is converted back to XML string and sent back to engine

To use it in your mods, you have to create a new script file that is called `modxml_<yourname>.script`. The yourname can be any string, it doesnt matter. The "modxml_" part must be in the filename.

Then, in this file, type:

```lua
function on_xml_read()
    RegisterScriptCallback("on_xml_read", function(xml_file_name, xml_obj)
        -- XML file i want to change
        local xml_to_change = [[text\eng\_game_version.xml]]

        -- Check if its the file i want to change
        if xml_file_name == xml_to_change then
            -- Here is my code to change XML
        end
    end)
end
```

What this does is creating a function on_xml_read that will be auto-called from dxml_core.script. This function will register function for new callback "on_xml_read", which accepts two arguments:

* xml_file_name - current XML filename that engine is processing (for example: text\eng\_game_version.xml)
* xml_obj - the object described above

**WARNING:** DXML won't process translation strings other than from eng/rus folders and `gameplay\character_desc_general.xml` file. For how to manipulate that file check "Additional functions" paragraph below.

To understand, what can be done with xml_obj and what functions it provides, let's take a look at typical usecases:

---

### Case 1: inserting new XML data

**Examples: insert new dialog into gameplay\dialogs.xml, insert new scope texture into ui\scopes.xml**

This is the simplest case of using DXML, where you just want to insert new data, much like `#include` directive in XML files

To do this, you can use `xml_obj:insertFromXMLString` function as in example below:

```lua
function on_xml_read()
    RegisterScriptCallback("on_xml_read", function(xml_file_name, xml_obj)
        -- XML file i want to change
        local xml_to_change = [[ui\scopes.xml]]

        -- Check if its the file i want to change
        if xml_file_name == xml_to_change then
            -- Here is my code to change XML
            local my_new_scope = 
[[
<wpn_crosshair_bino x="0" y="0" width="2048" height="1536">
    <auto_static x="0" y="0" width="1024" height="768" stretch="1">
    <texture>wpn_crosshair_bino</texture>
#include "gameplay\character_criticals.xml"
    </auto_static>
</wpn_crosshair_bino>
]]
            xml_obj:insertFromXMLString(my_new_scope)
        end
    end)
end
```

This example adds new scope texture into scopes.xml.
DXML supports `#include` directive to include other xml files into string. `#include` must start from the beginning of the line like in the example.
(BTW: For actually adding new scope you don't need include anything, its just an example. The `#include` is necessary when working with adding new special npcs or such)

insertFromXMLString method has these arguments:

* "xml_string" - XML string to process
* "where" - the element in xml_obj in which new data will be inserted argument is optional and specifies an element subtable of self.xml_table to insert (default - root element)
* "pos" argument is optional and specifies position to insert (default - to the end)
* "useRootNode" argument is optional and will hint DXML to insert contents inside the root node if it has one instead of whole string

The function returns the position of first inserted element in "where"

---

### Case 1.1: inserting new XML data from file

**Examples: insert new dialog into gameplay\dialogs.xml from file plugins\new_dialog.xml**

This case will insert new data from a xml file `plugins\new_dialog.xml` into `gameplay\dialogs.xml`.

To do this, you can use `xml_obj:insertFromXMLFile` function, where you specify the path to the file and the arguments, which are exactly the same as in `xml_obj:insertFromXMLString` function.

The path argument should be a path to the file WITH EXTENSION (example: `[[plugins\new_dialog.xml]]`).

The base folder for xml files to read is gamedata/configs, for example if the path provided is `plugins\new_dialog.xml`, then the file should exist in `gamedata/configs/plugins/new_dialog.xml`.

If the file has failed to read, the game will crash with the error message displaying what happened. 

```lua
function on_xml_read()
    RegisterScriptCallback("on_xml_read", function(xml_file_name, xml_obj)
        -- XML file i want to change
        local xml_to_change = [[gameplay\dialogs.xml]]

        -- Check if its the file i want to change
        if xml_file_name == xml_to_change then
            -- Here is my code to change XML
            xml_obj:insertFromXMLFile([[plugins\new_dialog.xml]])
        end
    end)
end
```

---

## Case 2: inserting new XML data in specified element

**Example: insert new menu item into ui\ui_mm_main.xml**

In order to correctly insert new main menu item, we need to find the <menu_main> element first.

To find an element, you can utilize CSS-like selectors in `query` function. An example of finding <menu_main> element would be:

```lua
local res = xml_obj:query("menu_main")
```

The function returns a table with all found elements matching this query.

The element is represented by table with following fields:

* el = element type, consists of type symbol, name and attributes. The type symbols are:
  * "<" - node element
  * "#" - text element
* parent = pointer to a table that contains this element
* kids = table that contains all children of this element

Then we check if table is not empty (the element exists) and insert our xml string with new menu item in position before the end

```lua
if is_not_empty(res) then
    local el = res[1]
    xml_obj:insertFromXMLString([[<btn name="btn_mcm" caption="ui_mm_menu_mcm"/>]], el, #el.kids)
```

Full code would be:

```lua
function on_xml_read()
    RegisterScriptCallback("on_xml_read", function(xml_file_name, xml_obj)
        -- XML file i want to change
        local xml_to_change = [[ui\ui_mm_main.xml]]

        -- Check if its the file i want to change
        if xml_file_name == xml_to_change then
            -- Here is my code to change XML
            local mcm_menu = [[<btn name="btn_mcm" caption="ui_mm_menu_mcm" />]]
            local res = xml_obj:query("menu_main")
            if is_not_empty(res) then
                local el = res[1]
                xml_obj:insertFromXMLString(mcm_menu, el, #el.kids)
            end
        end
    end)
end
```

But, if there is already an MCM menu, then you might get duplicated element inside. So we have to check if its already exists before insertion. We can check if element `<btn name="btn_mcm">` exists before proceeding by utilizing `query` again.

Here, similar to CSS, we want to find `<btn>` element with attribute `name="btn_mcm"` first. if its found - then make early return from the function

```lua
    if is_not_empty(xml_obj:query("menu_main btn[name=btn_mcm]")) then
        printf("MCM button already exists")
        return
    end
```

Full list of available CSS-like selectors are:

* " " (space) - find children of elemenents (can be any depth inside)
* ">" - find direct children
* "+" - find first sibling of element
* "~" - find all siblings
* "[attr1=value1]" - describes attribute `attr1` with value `value1`. To find element that matches multiple attributes you can use `[attr1=value1][attr2=value2]` and so on

---

## Case 3: Change text inside element

**Example: change text of game version in text\eng\\_game_version.xml**

Getting and setting text is possible if element contains raw text inside, like `<text attr1="value1">My Text</text>`.
To get text, you can use `getText(element)` function and to set it use `setText(element)`.
Example below of appending text to existing text in element

```lua
function on_xml_read()
	RegisterScriptCallback("on_xml_read", function(xml_file_name, xml_obj)
		if xml_file_name == [[text\eng\_game_version.xml]]
		or xml_file_name == [[text\rus\_game_version.xml]]
		then
			-- Find string element with "id=ui_st_game_version" text inside it
			local res = xml_obj:query("string[id=ui_st_game_version] > text")
			if res[1] then
				local el = res[1]
				local el_text = xml_obj:getText(el)
				if el_text then
					-- Set new text
					xml_obj:setText(el, el_text .. ". Modified exes (DLTX, DXML, Shader Scopes, SSS)")
				end
			end
		end
	end)
end
```

---

## Case 4: changing attribute of element

**Example: change position of money display in ui\ui_inventory.xml**

First, find `<money>` element inside `<player>` element and then change its x, y attributes by using `setElementAttr` function

`setElementAttr(el, args)`

* el - element found using `query`
* args - table of attributes ({attr1 = value1, attr2 = value2})

To get attributes of element, use `getElementAttr` function, it returns a table of attributes described above

`getElementAttr(el)`

* el - element found using `query`

To remove attributes of element, use `removeElementAttr` function

`removeElementAttr(el, args)`

* el - element found using `query`
* args - list of attributes to remove ({attr1, attr2})

```lua
function on_xml_read()
	RegisterScriptCallback("on_xml_read", function(xml_file_name, xml_obj)
		if xml_file_name == [[ui\ui_inventory.xml]]
		then
			local res = xml_obj:query("player > money")
			if res[1] then
				local el = res[1]
				xml_obj:setElementAttr(el, {x=20, y=60})
			end
		end
	end)
end
```

Full list of methods is described in dxml_core.script in `COnXmlRead` function

---

## Additional functions

For now, it is impossible to use xml_obj from DXML to process `gameplay\character_desc_general.xml`. This file includes all information about generic NPCs you see in the Zone (name, bio, community, etc). As an alternative, DXML provides additional callbacks in this case.

### on_specific_character_init

`on_specific_character_init` callback provides possibility to change NPCs' data. An example on how to use it:

```lua
function on_game_start()
    RegisterScriptCallback("on_specific_character_init", function(character_id, data)
  
        --character_id is the id attribute of <specific_character> tag (ie. "sim_default_csky_0_default_0")
        if character_id == "sim_default_csky_0_default_0" then
  
            -- change appearance of this npc to Beard
            data.visual = "actors\stalker_neutral\stalker_neutral_3_face_1"
        end
    end)
end
```

Full list of fields available in "data" table:
* name
* bio
* community
* icon
* start_dialog
* panic_threshold
* hit_probability_factor
* crouch_type
* mechanic_mode
* critical_wound_weights
* supplies
* visual
* npc_config
* snd_config
* terrain_sect
* rank_min
* rank_max
* reputation_min
* reputation_max
* money_min
* money_max
* money_infinitive

### on_specific_character_dialog_list

`on_specific_character_dialog_list` callback provides possibility to change available dialogs for NPCs. The callback sends character id and dialog list class object, that can be used to manupulate available dialogs. An example on how to use it:

```lua
function on_game_start()
    RegisterScriptCallback("on_specific_character_dialog_list", function(character_id, dialog_list)

        --character_id is the id attribute of <specific_character> tag (ie. "sim_default_csky_0_default_0")
        if character_id == "sim_default_csky_0_default_0" then

            -- Add dialog about playing Blackjack
            local res = dialog_list:add("cit_killers_minigame")

            if res then
                printf("adding dialog %s for %s, pos %s", "cit_killers_minigame", character_id, res)
            end
        end
    end)
end
```

Full list of methods in dialog_list
* find(regex) - find existing dialog by regex, returns last found dialog matching regex and its position in the list
* has(string) - check if dialog by string exists, returns the position of found dialog in the list
* add(string, pos) - adds new dialog in specified position (by default adds before break dialog string if it exists)
* add_first(dialog) - adds dialog in the beginning of the list
* add_last(dialog) - adds dialog in the end of the list
* remove(string) - removes dialog by the string
* get_dialogs() - returns the list of all dialogs available

---

## PS

See `dxml_core.script` for all methods and callbacks available
