#pragma once
// DDS names are relative to $game_textures$, without extension. Empty clears.
inline bool ValidatePreviewTexture(LPCSTR texture)
{
    if (!texture || !*texture) return true;
    if (xr_strlen(texture)>200 || strstr(texture,"..") || strchr(texture,':') ||
        texture[0]=='/' || texture[0]=='\\') return false;
    xr_string file=texture;file+=".dds";
    return !!FS.exist("$game_textures$",file.c_str());
}
