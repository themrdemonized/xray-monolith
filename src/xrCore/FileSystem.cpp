//----------------------------------------------------
// file: FileSystem.cpp
//----------------------------------------------------

#include "stdafx.h"
#pragma hdrstop

#include "cderr.h"
#include "commdlg.h"
#include "vfw.h"

EFS_Utils* xr_EFS = NULL;
//----------------------------------------------------
EFS_Utils::EFS_Utils()
{
}

EFS_Utils::~EFS_Utils()
{
}

void EFS_Utils::ExtractFileName(LPCSTR src, LPSTR dest, u32 dest_size)
{
    string_path name;
    _splitpath(src, 0, 0, name, 0);
    xr_strcpy(dest, dest_size, name);
}

xr_string EFS_Utils::ExtractFileName(LPCSTR src)
{
    string_path name;
    _splitpath(src, 0, 0, name, 0);
    return xr_string(name);
}

void EFS_Utils::ExtractFileExt(LPCSTR src, LPSTR dest, u32 dest_size)
{
    string_path ext;
    _splitpath(src, 0, 0, 0, ext);
    xr_strcpy(dest, dest_size, ext);
}

xr_string EFS_Utils::ExtractFileExt(LPCSTR src)
{
    string_path ext;
    _splitpath(src, 0, 0, 0, ext);
    return xr_string(ext);
}

void EFS_Utils::ExtractFilePath(LPCSTR src, LPSTR dest, u32 dest_size)
{
    string_path drive, dir;
    _splitpath(src, drive, dir, 0, 0);
    xr_sprintf(dest, dest_size, "%s%s", drive, dir);
}

xr_string EFS_Utils::ExtractFilePath(LPCSTR src)
{
    string_path drive, dir;
    _splitpath(src, drive, dir, 0, 0);
    return xr_string(drive) + dir;
}

xr_string EFS_Utils::ExcludeBasePath(LPCSTR full_path, LPCSTR excl_path)
{
    LPCSTR sub = strstr(full_path, excl_path);
    if (0 != sub) return xr_string(sub + xr_strlen(excl_path));
    else return xr_string(full_path);
}

xr_string EFS_Utils::ChangeFileExt(LPCSTR src, LPCSTR ext)
{
    string_path tmp;
    LPSTR src_ext = strext(src);
    if (src_ext)
    {
        size_t ext_pos = src_ext - src;
        CopyMemory(tmp, src, ext_pos);
        tmp[ext_pos] = 0;
    }
    else
    {
        xr_strcpy(tmp, sizeof(tmp), src);
    }
    xr_strcat(tmp, sizeof(tmp), ext);
    return xr_string(tmp);
}

xr_string EFS_Utils::ChangeFileExt(const xr_string& src, LPCSTR ext)
{
    return ChangeFileExt(src.c_str(), ext);
}

//----------------------------------------------------
void MakeFilter(string1024& dest, LPCSTR info, LPCSTR ext)
{
    u32 pos = 0;
    dest[0] = 0;

    if (ext)
    {
        pos += xr_sprintf(dest + pos, sizeof(dest) - pos, "%s(%s)", info, ext);
        dest[pos++] = 0;
        pos += xr_sprintf(dest + pos, sizeof(dest) - pos, "%s", ext);
        dest[pos++] = 0;

        int icnt = _GetItemCount(ext, ';');
        if (icnt > 1)
        {
            for (int idx = 0; idx < icnt; ++idx)
            {
                string64 buf;
                _GetItem(ext, idx, buf, ';');

                pos += xr_sprintf(dest + pos, sizeof(dest) - pos, "%s(%s)", info, buf);
                dest[pos++] = 0;
                pos += xr_sprintf(dest + pos, sizeof(dest) - pos, "%s", buf);
                dest[pos++] = 0;
            }
        }
        dest[pos++] = 0; 
    }
    else
    {
        pos += xr_sprintf(dest + pos, sizeof(dest) - pos, "All files(*.*)");
        dest[pos++] = 0;
        pos += xr_sprintf(dest + pos, sizeof(dest) - pos, "*.*");
        dest[pos++] = 0;
        dest[pos++] = 0;
    }
}

//------------------------------------------------------------------------------
UINT_PTR CALLBACK OFNHookProcOldStyle(HWND, UINT, WPARAM, LPARAM)
{
    return 0;
}

bool EFS_Utils::GetOpenNameInternal(LPCSTR initial, LPSTR buffer, int sz_buf, bool bMulti, LPCSTR offset,
                                    int start_flt_ext)
{
    VERIFY(buffer && (sz_buf > 0));
    FS_Path& P = *FS.get_path(initial);
    string1024 flt;
    MakeFilter(flt, P.m_FilterCaption ? P.m_FilterCaption : "", P.m_DefExt);

    OPENFILENAME ofn;
    Memory.mem_fill(&ofn, 0, sizeof(ofn));

    if (xr_strlen(buffer))
    {
        string_path dr;
        if (!(buffer[0] == '\\' && buffer[1] == '\\'))
        {
            _splitpath(buffer, dr, 0, 0, 0);

            if (0 == dr[0])
            {
                string_path bb;
                P._update(bb, buffer);
                xr_strcpy(buffer, sz_buf, bb);
            }
        }
    }
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = GetForegroundWindow();
    ofn.lpstrDefExt = P.m_DefExt;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = sz_buf;
    ofn.lpstrFilter = flt;
    ofn.nFilterIndex = start_flt_ext + 2;
    ofn.lpstrTitle = "Open a File";
    string512 path;
    xr_strcpy(path, (offset && offset[0]) ? offset : P.m_Path);
    ofn.lpstrInitialDir = path;
    ofn.Flags = OFN_PATHMUSTEXIST |
        OFN_FILEMUSTEXIST |
        OFN_HIDEREADONLY |
        OFN_NOCHANGEDIR |
        (bMulti ? OFN_ALLOWMULTISELECT | OFN_EXPLORER : 0);

    ofn.FlagsEx = OFN_EX_NOPLACESBAR;

    bool bRes = !!GetOpenFileName(&ofn);
    if (!bRes)
    {
        u32 err = CommDlgExtendedError();
        switch (err)
        {
        case FNERR_BUFFERTOOSMALL:
            Log("Too many files selected.");
            break;
        }
    }
    if (bRes && bMulti)
    {
        Log("buff=", buffer);
        int cnt = _GetItemCount(buffer, 0x0);
        if (cnt > 1)
        {
            string_path dir;
            string_path buf;
            char fns[16384]; 

            xr_strcpy(dir, sizeof(dir), buffer);
            xr_strcpy(fns, sizeof(fns), dir);
            xr_strcat(fns, sizeof(fns), "\\");
            xr_strcat(fns, sizeof(fns), _GetItem(buffer, 1, buf, 0x0));

            for (int i = 2; i < cnt; i++)
            {
                xr_strcat(fns, sizeof(fns), ",");
                xr_strcat(fns, sizeof(fns), dir);
                xr_strcat(fns, sizeof(fns), "\\");
                xr_strcat(fns, sizeof(fns), _GetItem(buffer, i, buf, 0x0));
            }
            xr_strcpy(buffer, sz_buf, fns);
        }
    }
    strlwr(buffer);
    return bRes;
}

bool EFS_Utils::GetSaveName(LPCSTR initial, string_path& buffer, LPCSTR offset, int start_flt_ext)
{
    FS_Path& P = *FS.get_path(initial);
    string1024 flt;

    LPCSTR def_ext = P.m_DefExt;
    if (false)
    {
        if (strstr(P.m_DefExt, "*."))
            def_ext = strstr(P.m_DefExt, "*.") + 2;
    }

    MakeFilter(flt, P.m_FilterCaption ? P.m_FilterCaption : "", def_ext);
    OPENFILENAME ofn;
    Memory.mem_fill(&ofn, 0, sizeof(ofn));
    if (xr_strlen(buffer))
    {
        string_path dr;
        if (!(buffer[0] == '\\' && buffer[1] == '\\'))
        {
            _splitpath(buffer, dr, 0, 0, 0);
            if (0 == dr[0]) P._update(buffer, buffer);
        }
    }
    ofn.hwndOwner = GetForegroundWindow();
    ofn.lpstrDefExt = def_ext;
    ofn.lpstrFile = buffer;
    ofn.lpstrFilter = flt;
    ofn.lStructSize = sizeof(ofn);
    ofn.nMaxFile = sizeof(buffer);
    ofn.nFilterIndex = start_flt_ext + 2;
    ofn.lpstrTitle = "Save a File";
    string512 path;
    xr_strcpy(path, (offset && offset[0]) ? offset : P.m_Path);
    ofn.lpstrInitialDir = path;
    ofn.Flags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.FlagsEx = OFN_EX_NOPLACESBAR;

    bool bRes = !!GetSaveFileName(&ofn);
    if (!bRes)
    {
        u32 err = CommDlgExtendedError();
        switch (err)
        {
        case FNERR_BUFFERTOOSMALL:
            Log("Too many file selected.");
            break;
        }
    }
    strlwr(buffer);
    return bRes;
}

//----------------------------------------------------
LPCSTR EFS_Utils::AppendFolderToName(LPSTR tex_name, u32 const tex_name_size, int depth, BOOL full_name)
{
    string256 _fn;
    xr_strcpy(tex_name, tex_name_size, AppendFolderToName(tex_name, _fn, sizeof(_fn), depth, full_name));
    return tex_name;
}

LPCSTR EFS_Utils::AppendFolderToName(LPCSTR src_name, LPSTR dest_name, u32 const dest_name_size, int depth,
                                     BOOL full_name)
{
    shared_str tmp = src_name;
    LPCSTR s = src_name;
    LPSTR d = dest_name;
    int sv_depth = depth;
    for (; *s && depth; s++, d++)
    {
        if (*s == '_')
        {
            depth--;
            *d = '\\';
        }
        else { *d = *s; }
    }
    if (full_name)
    {
        *d = 0;
        if (depth < sv_depth) xr_strcat(dest_name, dest_name_size, *tmp);
    }
    else
    {
        for (; *s; s++, d++) *d = *s;
        *d = 0;
    }
    return dest_name;
}

LPCSTR EFS_Utils::GenerateName(LPCSTR base_path, LPCSTR base_name, LPCSTR def_ext, LPSTR out_name,
                               u32 const out_name_size)
{
    int cnt = 0;
    string_path fn;
    if (base_name)
        strconcat(sizeof(fn), fn, base_path, base_name, def_ext);
    else
        xr_sprintf(fn, sizeof(fn), "%s%02d%s", base_path, cnt++, def_ext);

    while (FS.exist(fn))
        if (base_name)
            xr_sprintf(fn, sizeof(fn), "%s%s%02d%s", base_path, base_name, cnt++, def_ext);
        else
            xr_sprintf(fn, sizeof(fn), "%s%02d%s", base_path, cnt++, def_ext);
    xr_strcpy(out_name, out_name_size, fn);
    return out_name;
}
