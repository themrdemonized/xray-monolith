Normal use (easy mode)

- Put the files and directories into the "mod" directory that would usually go into "gamedata"
 + example: "gamedata\configs\test.ltx" needs to be "mod\configs\test.ltx" here
- Use "xrPackage.bat" to create "mod.db0"
- Rename "mod.db0" into whatever you want to call your mod, keep the ".db0" extension
 + example: rename "mod.db0" into "mycoolmod.db0"
- Put it into "db\mods" in the Anomaly directory

Advanced use (pro mode)

- Rename "mod" directory to the name of your mod
 + example: rename "mod" to "mycoolmod"
- Change the name of the directory in "xrPackage.bat"
 + example: change "xrCompress mod -ltx xrCompress.ltx -pack -1024 -db" to "xrCompress mycoolmod -ltx xrCompress.ltx -pack -1024 -db"
- Change this field in "xrCompress.ltx": (DO NOT TOUCH ANYTHING ELSE IN THE LTX FILE !!!)
 + creator = "Modder"
 + example: change creator = "Modder" to creator = "ItzeMeModdio"
- Now using "xrPackage.bat" will create "mycoolmod.db0" if you run it

NOTES:
- I recommend to delete the old .db0 file before compressing a new one
- If your mod changes files that are also changed in an Anomaly Update, you'll have to create a patch for your mod as well
- A patch for your mod should contain the corrected files that are supposed to overwrite the file in the Anomaly Updates
- Make sure it's compatible with the latest Anomaly Updates, otherwise you might undo any fix that Anomaly Updates provide
- Name a patch for a mod like this: "update-999-MODNAME.db0"
- Updated for mods have to be in "db\updates" and NOT in "db\mods" to be effective
