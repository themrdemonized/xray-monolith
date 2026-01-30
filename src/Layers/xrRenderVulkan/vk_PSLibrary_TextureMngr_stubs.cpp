// xrRenderVulkan - Vulkan renderer for X-Ray Engine
// Copyright (c) 2024-2026 Egor Babushkin (https://github.com/babasha)
// SPDX-License-Identifier: MIT

// ============================================================================
// Full CPSLibrary implementation for Vulkan renderer
// Ported from xrRender/PSLibrary.cpp
// ============================================================================

#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/PSLibrary.h"
#include "../xrRender/ParticleEffectDef.h"
#include "../xrRender/ParticleGroup.h"
#include "../xrRender/TextureDescrManager.h"
#include "../xrRender/dxWallMarkArray.h"
#include "../xrRender/ETextureParams.h"  // For STextureParams and .thm loading
#include "vk_detail_scaler.h"            // For cl_dt_scaler

// Include particle definitions (compatible with both DX and Vulkan)
#include "../../xrParticles/psystem.h"

// Detail texture range (eye-params)
float r__dtex_range = 50.0f;

// Helper predicates for sorting and searching
static bool ped_sort_pred(const PS::CPEDef* a, const PS::CPEDef* b)
{
    return xr_strcmp(a->Name(), b->Name()) < 0;
}

static bool pgd_sort_pred(const PS::CPGDef* a, const PS::CPGDef* b)
{
    return xr_strcmp(a->m_Name, b->m_Name) < 0;
}

static bool ped_find_pred(const PS::CPEDef* a, LPCSTR b)
{
    return xr_strcmp(a->Name(), b) < 0;
}

static bool pgd_find_pred(const PS::CPGDef* a, LPCSTR b)
{
    return xr_strcmp(a->m_Name, b) < 0;
}

// ============================================================================
// CPSLibrary - Particle System Library
// ============================================================================

void CPSLibrary::OnCreate()
{
    Msg("[Vulkan] CPSLibrary::OnCreate() - Loading particle definitions...");

    // Load particle definitions from gamedata/particles/
    FS_FileSet files;
    string_path _path;

    FS.update_path(_path, "$game_particles$", "");
    FS.file_list(files, _path, FS_ListFiles, "*.pe,*.pg");

    if (files.empty())
    {
        Msg("![Vulkan] No particle files found in %s", _path);
        return;
    }

    string_path p_path, p_name, p_ext;
    u32 loaded_pe = 0;
    u32 loaded_pg = 0;

    for (const FS_File& f : files)
    {
        _splitpath(f.name.c_str(), 0, p_path, p_name, p_ext);
        FS.update_path(_path, "$game_particles$", f.name.c_str());

        if (!FS.exist(_path))
        {
            Msg("![Vulkan] Particle file not found: %s", _path);
            continue;
        }

        CInifile ini(_path, TRUE, TRUE, FALSE);
        xr_sprintf(_path, sizeof(_path), "%s%s", p_path, p_name);

        if (0 == stricmp(p_ext, ".pe"))
        {
            // Particle Effect Definition
            PS::CPEDef* def = xr_new<PS::CPEDef>();
            def->m_Name = _path;

            if (def->Load2(ini))
            {
                m_PEDs.push_back(def);
                loaded_pe++;

                // Note: CreateShader() called later after sorting
            }
            else
            {
                Msg("![Vulkan] Failed to load particle effect: %s", _path);
                xr_delete(def);
            }
        }
        else if (0 == stricmp(p_ext, ".pg"))
        {
            // Particle Group Definition
            PS::CPGDef* def = xr_new<PS::CPGDef>();
            def->m_Name = _path;

            if (def->Load2(ini))
            {
                m_PGDs.push_back(def);
                loaded_pg++;
            }
            else
            {
                Msg("![Vulkan] Failed to load particle group: %s", _path);
                xr_delete(def);
            }
        }
    }

    // Sort for faster binary search
    std::sort(m_PEDs.begin(), m_PEDs.end(), ped_sort_pred);
    std::sort(m_PGDs.begin(), m_PGDs.end(), pgd_sort_pred);

    // Create shaders for all particle effects
    // Note: In Vulkan, CreateShader() should use Vulkan shader loading
    for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
    {
        // TODO: Vulkan-specific shader creation
        // For now, skip CreateShader() as it uses DX-specific code
        // (*e_it)->CreateShader();
    }

    Msg("[Vulkan] Loaded %u particle effects and %u particle groups", loaded_pe, loaded_pg);
}

void CPSLibrary::OnDestroy()
{
    Msg("[Vulkan] CPSLibrary::OnDestroy() - Cleaning up particle library...");

    // Destroy shaders for all effects
    for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
    {
        // TODO: Vulkan-specific shader cleanup
        // (*e_it)->DestroyShader();
    }

    // Delete all particle effect definitions
    for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
        xr_delete(*e_it);
    m_PEDs.clear();

    // Delete all particle group definitions
    for (PS::PGDIt g_it = m_PGDs.begin(); g_it != m_PGDs.end(); ++g_it)
        xr_delete(*g_it);
    m_PGDs.clear();

    Msg("[Vulkan] CPSLibrary destroyed");
}

// ============================================================================
// Find methods - Search for particle definitions by name
// ============================================================================

PS::PEDIt CPSLibrary::FindPEDIt(LPCSTR Name)
{
    if (!Name) return m_PEDs.end();

    // Binary search (m_PEDs is sorted)
    PS::PEDIt I = std::lower_bound(m_PEDs.begin(), m_PEDs.end(), Name, ped_find_pred);

    if (I == m_PEDs.end() || (0 != xr_strcmp((*I)->m_Name, Name)))
        return m_PEDs.end();
    else
        return I;
}

PS::CPEDef* CPSLibrary::FindPED(LPCSTR Name)
{
    PS::PEDIt it = FindPEDIt(Name);
    return (it == m_PEDs.end()) ? nullptr : *it;
}

PS::PGDIt CPSLibrary::FindPGDIt(LPCSTR Name)
{
    if (!Name) return m_PGDs.end();

    // Binary search (m_PGDs is sorted)
    PS::PGDIt I = std::lower_bound(m_PGDs.begin(), m_PGDs.end(), Name, pgd_find_pred);

    if (I == m_PGDs.end() || (0 != xr_strcmp((*I)->m_Name, Name)))
        return m_PGDs.end();
    else
        return I;
}

PS::CPGDef* CPSLibrary::FindPGD(LPCSTR Name)
{
    PS::PGDIt it = FindPGDIt(Name);
    return (it == m_PGDs.end()) ? nullptr : *it;
}

// ============================================================================
// Iterator interface for particle groups
// ============================================================================

PS::CPGDef const* const* CPSLibrary::particles_group_begin() const
{
    return (m_PGDs.size() ? &*m_PGDs.begin() : nullptr);
}

PS::CPGDef const* const* CPSLibrary::particles_group_end() const
{
    return (m_PGDs.size() ? &*m_PGDs.end() : nullptr);
}

void CPSLibrary::particles_group_next(PS::CPGDef const* const*& iterator) const
{
    VERIFY(iterator);
    VERIFY(iterator >= particles_group_begin());
    VERIFY(iterator < particles_group_end());
    ++iterator;
}

shared_str const& CPSLibrary::particles_group_id(PS::CPGDef const& particles_group) const
{
    return particles_group.m_Name;
}

// ============================================================================
// CTextureDescrMngr - Full Implementation for Vulkan
// ============================================================================
//
// Ported from xrRender/TextureDescrManager.cpp
// Loads .thm files (texture metadata) from $game_textures$ and $level$
//
// .thm files contain:
// - bump_name: normal map filename
// - material: material ID (0-3) + weight for blending
// - detail_name: detail texture filename
// - detail_scale: detail texture scale
// - bump_mode: None/Use/UseParallax
//
// ============================================================================

// Helper function to remove texture extensions
static void fix_texture_thm_name(LPSTR fn)
{
    LPSTR _ext = strext(fn);
    if (_ext &&
        (0 == stricmp(_ext, ".tga") ||
         0 == stricmp(_ext, ".thm") ||
         0 == stricmp(_ext, ".dds") ||
         0 == stricmp(_ext, ".bmp") ||
         0 == stricmp(_ext, ".ogm")))
        *_ext = 0;
}

// Struct for thread loading
struct TH_LoadTHM_Vulkan
{
    LPCSTR initial;
    CTextureDescrMngr::map_TD& s_texture_details;
    CTextureDescrMngr::map_CS& s_detail_scalers;
};

// Thread function wrapper
static void LoadTHMThread_Vulkan(void* args)
{
    TH_LoadTHM_Vulkan* p = (TH_LoadTHM_Vulkan*)args;
    CTextureDescrMngr::LoadTHM(p->initial, p->s_texture_details, p->s_detail_scalers);
    xr_delete(p);
}

// ============================================================================
// LoadTHM - Load all .thm files from a directory (static method)
// ============================================================================
void CTextureDescrMngr::LoadTHM(LPCSTR initial,
                                 CTextureDescrMngr::map_TD& s_texture_details,
                                 CTextureDescrMngr::map_CS& s_detail_scalers)
{
    Msg("[Vulkan] Loading .thm files from: %s", initial);

    // Find all .thm files in directory
    FS_FileSet flist;
    FS.file_list(flist, initial, FS_ListFiles, "*.thm");

    if (flist.empty())
    {
        Msg("[Vulkan] No .thm files found in %s", initial);
        return;
    }

    STextureParams tp;
    string_path fn;
    u32 loaded_count = 0;

    for (const FS_File& fs_iter : flist)
    {
        // Build full path to .thm file
        FS.update_path(fn, initial, fs_iter.name.c_str());

        // Open .thm file
        IReader* F = FS.r_open(fn);
        if (!F)
        {
            Msg("![Vulkan] Failed to open .thm file: %s", fn);
            continue;
        }

        // Get texture name (without extension)
        xr_strcpy(fn, fs_iter.name.c_str());
        fix_texture_thm_name(fn);

        // Read .thm file structure
        if (!F->find_chunk(THM_CHUNK_TYPE))
        {
            Msg("![Vulkan] Invalid .thm file (no THM_CHUNK_TYPE): %s", fn);
            FS.r_close(F);
            continue;
        }

        F->r_u32();  // Read type
        tp.Clear();
        tp.Load(*F);
        FS.r_close(F);

        // Only process Image, Terrain, and NormalMap textures
        if (STextureParams::ttImage == tp.type ||
            STextureParams::ttTerrain == tp.type ||
            STextureParams::ttNormalMap == tp.type)
        {
            texture_desc& desc = s_texture_details[fn];
            cl_dt_scaler*& dts = s_detail_scalers[fn];

            // ================================================================
            // Detail texture (Phase 2.33)
            // ================================================================
            if (tp.detail_name.size() &&
                tp.flags.is_any(STextureParams::flDiffuseDetail | STextureParams::flBumpDetail))
            {
                if (desc.m_assoc)
                    xr_delete(desc.m_assoc);

                desc.m_assoc = xr_new<texture_assoc>();
                desc.m_assoc->detail_name = tp.detail_name;

                // Create or update detail scaler
                if (dts)
                    dts->scale = tp.detail_scale;
                else
                    dts = xr_new<cl_dt_scaler>(tp.detail_scale);

                desc.m_assoc->usage = 0;

                // Mark usage flags
                if (tp.flags.is(STextureParams::flDiffuseDetail))
                    desc.m_assoc->usage |= (1 << 0);

                if (tp.flags.is(STextureParams::flBumpDetail))
                    desc.m_assoc->usage |= (1 << 1);
            }

            // ================================================================
            // Material parameters (Phase 2.33)
            // ================================================================
            if (desc.m_spec)
                xr_delete(desc.m_spec);

            desc.m_spec = xr_new<texture_spec>();

            // Material ID + weight (0-3 with blending)
            desc.m_spec->m_material = tp.material + (tp.material < 4 ? tp.material_weight : 0);
            desc.m_spec->m_use_steep_parallax = false;

            // ================================================================
            // Bump mapping (normal maps)
            // ================================================================
            if (tp.bump_mode == STextureParams::tbmUse)
            {
                // Standard normal mapping
                desc.m_spec->m_bump_name = tp.bump_name;
            }
            else if (tp.bump_mode == STextureParams::tbmUseParallax)
            {
                // Parallax occlusion mapping
                desc.m_spec->m_bump_name = tp.bump_name;
                desc.m_spec->m_use_steep_parallax = true;
            }

            loaded_count++;
        }
    }

    Msg("[Vulkan] Loaded %u .thm files from %s", loaded_count, initial);
}

// ============================================================================
// CTextureDescrMngr Lifecycle
// ============================================================================

CTextureDescrMngr::~CTextureDescrMngr()
{
    // Clean up detail scalers
    for (auto& it : m_detail_scalers)
    {
        xr_delete(it.second);
    }
    m_detail_scalers.clear();
}

void CTextureDescrMngr::Load()
{
    Msg("[Vulkan] CTextureDescrMngr::Load() - Loading .thm files...");

    // Load .thm files from two locations in parallel:
    // 1. $game_textures$ - gamedata/textures/
    // 2. $level$ - current level textures

    TH_LoadTHM_Vulkan* gtex = xr_new<TH_LoadTHM_Vulkan>();
    gtex->initial = "$game_textures$";
    gtex->s_texture_details = m_texture_details;
    gtex->s_detail_scalers = m_detail_scalers;

    TH_LoadTHM_Vulkan* lvl = xr_new<TH_LoadTHM_Vulkan>();
    lvl->initial = "$level$";
    lvl->s_texture_details = m_texture_details;
    lvl->s_detail_scalers = m_detail_scalers;

    // Spawn loading threads
    thread_spawn(LoadTHMThread_Vulkan, "Vulkan THM Loader 1", 0, gtex);
    thread_spawn(LoadTHMThread_Vulkan, "Vulkan THM Loader 2", 0, lvl);

    // Wait a bit for threads to start
    Sleep(5);

    Msg("[Vulkan] CTextureDescrMngr::Load() - Threads spawned, loading in background");
}

void CTextureDescrMngr::UnLoad()
{
    Msg("[Vulkan] CTextureDescrMngr::UnLoad() - Unloading .thm data...");

    // Clean up all texture descriptors
    for (auto& it : m_texture_details)
    {
        xr_delete(it.second.m_assoc);
        xr_delete(it.second.m_spec);
    }
    m_texture_details.clear();

    Msg("[Vulkan] CTextureDescrMngr unloaded");
}

// ============================================================================
// Accessors - Get data from loaded .thm files
// ============================================================================

shared_str CTextureDescrMngr::GetBumpName(shared_str const& tex_name) const
{
    auto I = m_texture_details.find(tex_name);
    if (I != m_texture_details.end())
    {
        if (I->second.m_spec)
        {
            return I->second.m_spec->m_bump_name;
        }
    }
    return "";
}

float CTextureDescrMngr::GetMaterial(shared_str const& tex_name) const
{
    auto I = m_texture_details.find(tex_name);
    if (I != m_texture_details.end())
    {
        if (I->second.m_spec)
        {
            return I->second.m_spec->m_material;
        }
    }
    return 1.0f;  // Default: Blin-Phong material
}

int CTextureDescrMngr::UseSteepParallax(shared_str const& tex_name) const
{
    auto I = m_texture_details.find(tex_name);
    if (I != m_texture_details.end())
    {
        if (I->second.m_spec)
        {
            return I->second.m_spec->m_use_steep_parallax;
        }
    }
    return FALSE;
}

int CTextureDescrMngr::GetDetailTexture(shared_str const& tex_name,
                                         char const*& res,
                                         R_constant_setup*& CS) const
{
    auto I = m_texture_details.find(tex_name);
    if (I != m_texture_details.end())
    {
        if (I->second.m_assoc)
        {
            texture_assoc* TA = I->second.m_assoc;
            res = TA->detail_name.c_str();

            // Get detail scaler constant setup
            auto It2 = m_detail_scalers.find(tex_name);
            CS = (It2 == m_detail_scalers.end()) ? nullptr : It2->second;

            return TRUE;
        }
    }
    return FALSE;
}

// ============================================================================
// dxWallMarkArray stub implementation
// ============================================================================

resptr_core<Shader, resptrcode_shader>* dxWallMarkArray::dxGenerateWallmark()
{
    // TODO: Generate wallmark shader
    // For now return nullptr - wallmarks won't work but won't crash
    Msg("[Vulkan] dxWallMarkArray::dxGenerateWallmark() - stub");
    return nullptr;
}
