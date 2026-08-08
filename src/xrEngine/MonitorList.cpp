// MonitorList.cpp — monitor enumeration for the vid_monitor dropdown.
//
// Enumerates monitors via EnumDisplayMonitors + GetMonitorInfoA,
// looks up friendly names via QueryDisplayConfig / DisplayConfigGetDeviceInfo,
// and builds vid_monitor_token + a parallel HMONITOR sidecar vector.
//
// QueryDisplayConfig / DisplayConfigGetDeviceInfo are Windows 7+ APIs.
// They are loaded dynamically (same style as DPI-awareness in x_ray.cpp) so
// the translation unit compiles cleanly against the old _WIN32_WINNT value
// (0x0550) that xrEngine stdafx.h defines.

#include "stdafx.h"
#include "MonitorList.h"

#pragma comment(lib, "cfgmgr32.lib")
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>


ENGINE_API xr_token* vid_monitor_token = nullptr;
ENGINE_API xr_string  vid_monitor_name  = "Auto";
ENGINE_API volatile long g_monitor_list_dirty = 0;

static xr_vector<HMONITOR> s_monitor_handles;

typedef enum _DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY_LOCAL
{
    DCVOT_OTHER             = -1,
    DCVOT_HD15              = 0,
    DCVOT_SVIDEO            = 1,
    DCVOT_COMPOSITE_VIDEO   = 2,
    DCVOT_COMPONENT_VIDEO   = 3,
    DCVOT_DVI               = 4,
    DCVOT_HDMI              = 5,
    DCVOT_LVDS              = 6,
    DCVOT_D_JPN             = 8,
    DCVOT_SDI               = 9,
    DCVOT_DISPLAYPORT_EXTERNAL = 10,
    DCVOT_DISPLAYPORT_EMBEDDED = 11,
    DCVOT_UDI_EXTERNAL      = 12,
    DCVOT_UDI_EMBEDDED      = 13,
    DCVOT_SDTVDONGLE        = 14,
    DCVOT_MIRACAST          = 15,
    DCVOT_INDIRECT_WIRED    = 16,
    DCVOT_INDIRECT_VIRTUAL  = 17,
    DCVOT_INTERNAL          = 0x80000000,
} DCVOT_LOCAL;

// Minimal structs — just enough fields to read what we need.
// Offsets must match the Win7 SDK ABI; we only use the first few fields.
// Default alignment (no pack pragma) matches the real SDK ABI since every
// field is already 4-byte aligned.

struct LUID_LOCAL { DWORD LowPart; LONG HighPart; };

struct DISPLAYCONFIG_PATH_SOURCE_INFO_LOCAL
{
    LUID_LOCAL adapterId;
    UINT32     id;
    union { UINT32 modeInfoIdx; UINT32 cloneGroupId; };
    UINT32     statusFlags;
};

struct DISPLAYCONFIG_PATH_TARGET_INFO_LOCAL
{
    LUID_LOCAL adapterId;
    UINT32     id;
    union { UINT32 modeInfoIdx; struct { UINT32 desktopModeInfoIdx:16; UINT32 targetModeInfoIdx:16; }; };
    UINT32     outputTechnology; // matches DCVOT_LOCAL values
    UINT32     rotation;
    UINT32     scaling;
    struct { UINT32 Numerator; UINT32 Denominator; } refreshRate;
    UINT32     scanLineOrdering;
    BOOL       targetAvailable;
    UINT32     statusFlags;
};

struct DISPLAYCONFIG_PATH_INFO_LOCAL
{
    DISPLAYCONFIG_PATH_SOURCE_INFO_LOCAL sourceInfo;
    DISPLAYCONFIG_PATH_TARGET_INFO_LOCAL targetInfo;
    UINT32 flags;
};

struct DISPLAYCONFIG_MODE_INFO_LOCAL
{
    UINT32     infoType;
    UINT32     id;
    LUID_LOCAL adapterId;
    // Union body never read; pad to the SDK size of 64 bytes.
    UINT8      _pad[48];
};

struct DISPLAYCONFIG_DEVICE_INFO_HEADER_LOCAL
{
    UINT32     type;
    UINT32     size;
    LUID_LOCAL adapterId;
    UINT32     id;
};

struct DISPLAYCONFIG_SOURCE_DEVICE_NAME_LOCAL
{
    DISPLAYCONFIG_DEVICE_INFO_HEADER_LOCAL header;
    WCHAR viewGdiDeviceName[32];
};

struct DISPLAYCONFIG_TARGET_DEVICE_NAME_FLAGS_LOCAL { UINT32 value; };
struct DISPLAYCONFIG_TARGET_DEVICE_NAME_LOCAL
{
    DISPLAYCONFIG_DEVICE_INFO_HEADER_LOCAL    header;
    DISPLAYCONFIG_TARGET_DEVICE_NAME_FLAGS_LOCAL flags;
    UINT32 outputTechnology;
    UINT16 edidManufactureId;
    UINT16 edidProductCodeId;
    UINT32 connectorInstance;
    WCHAR  monitorFriendlyDeviceName[64];
    WCHAR  monitorDevicePath[128];
};

#define DCDI_GET_SOURCE_NAME  1
#define DCDI_GET_TARGET_NAME  2

typedef LONG (WINAPI* PFN_GetDisplayConfigBufferSizes)(UINT32, UINT32*, UINT32*);
typedef LONG (WINAPI* PFN_QueryDisplayConfig)(UINT32, UINT32*, void*, UINT32*, void*, void*);
typedef LONG (WINAPI* PFN_DisplayConfigGetDeviceInfo)(void*);


namespace
{
    struct MonitorInfo
    {
        xr_string friendly_name;
        u32       connector_instance = 0;
        u32       output_technology  = 0;
    };

    using GdiToInfoMap = xr_map<xr_string, MonitorInfo>;

    const char* ConnectorTechString(u32 tech)
    {
        switch (tech)
        {
        case DCVOT_DISPLAYPORT_EXTERNAL:
        case DCVOT_DISPLAYPORT_EMBEDDED:
            return "DP";
        case DCVOT_HDMI:
            return "HDMI";
        case DCVOT_DVI:
            return "DVI";
        case DCVOT_HD15:
            return "VGA";
        case DCVOT_INTERNAL:
            return "INT";
        default:
            return nullptr;
        }
    }

    // Fetch the PnP "Device description" for a monitor, given its DISPLAYCONFIG
    // monitorDevicePath (a device-interface path like
    // "\\?\DISPLAY#LGD06B3#5&abc&0&UID265988#{e6f07b5f-...}").  Returns true
    // on success and writes up to bufSize-1 bytes of ACP-encoded description
    // into buf.  Used as a fallback when the DisplayConfig friendlyName is
    // empty (typical for internal laptop panels).
    bool TryGetDeviceDesc(const WCHAR* devicePath, char* buf, size_t bufSize)
    {
        if (!devicePath || !devicePath[0] || !buf || bufSize == 0)
            return false;

        WCHAR instanceId[512] = {};
        ULONG size = sizeof(instanceId);
        DEVPROPTYPE propType = 0;
        CONFIGRET cr = CM_Get_Device_Interface_PropertyW(
            devicePath,
            &DEVPKEY_Device_InstanceId,
            &propType,
            reinterpret_cast<PBYTE>(instanceId),
            &size,
            0);
        if (cr != CR_SUCCESS || propType != DEVPROP_TYPE_STRING)
            return false;

        DEVINST devInst = 0;
        cr = CM_Locate_DevNodeW(&devInst, instanceId, CM_LOCATE_DEVNODE_NORMAL);
        if (cr != CR_SUCCESS)
            return false;

        WCHAR desc[256] = {};
        size = sizeof(desc);
        propType = 0;
        cr = CM_Get_DevNode_PropertyW(
            devInst,
            &DEVPKEY_Device_DeviceDesc,
            &propType,
            reinterpret_cast<PBYTE>(desc),
            &size,
            0);
        if (cr != CR_SUCCESS || propType != DEVPROP_TYPE_STRING || desc[0] == L'\0')
            return false;

        WideCharToMultiByte(CP_ACP, 0, desc, -1, buf, (int)bufSize - 1,
                            nullptr, nullptr);
        buf[bufSize - 1] = '\0';
        return true;
    }

    void BuildFriendlyNameMap(GdiToInfoMap& out)
    {
        HMODULE hUser32 = GetModuleHandleA("user32.dll");
        if (!hUser32) return;

        auto pfnGetBufs = (PFN_GetDisplayConfigBufferSizes)
            GetProcAddress(hUser32, "GetDisplayConfigBufferSizes");
        auto pfnQuery = (PFN_QueryDisplayConfig)
            GetProcAddress(hUser32, "QueryDisplayConfig");
        auto pfnGetDevInfo = (PFN_DisplayConfigGetDeviceInfo)
            GetProcAddress(hUser32, "DisplayConfigGetDeviceInfo");

        if (!pfnGetBufs || !pfnQuery || !pfnGetDevInfo)
            return;

        UINT32 num_paths = 0, num_modes = 0;
        if (pfnGetBufs(QDC_ONLY_ACTIVE_PATHS, &num_paths, &num_modes) != ERROR_SUCCESS)
            return;
        if (num_paths == 0)
            return;

        xr_vector<DISPLAYCONFIG_PATH_INFO_LOCAL>  paths(num_paths);
        xr_vector<DISPLAYCONFIG_MODE_INFO_LOCAL>  modes(num_modes);

        if (pfnQuery(QDC_ONLY_ACTIVE_PATHS, &num_paths,
                     paths.data(), &num_modes, modes.data(), nullptr) != ERROR_SUCCESS)
            return;

        for (UINT32 i = 0; i < num_paths; ++i)
        {
            const DISPLAYCONFIG_PATH_INFO_LOCAL& path = paths[i];

            DISPLAYCONFIG_SOURCE_DEVICE_NAME_LOCAL src = {};
            src.header.type      = DCDI_GET_SOURCE_NAME;
            src.header.size      = sizeof(src);
            src.header.adapterId = path.sourceInfo.adapterId;
            src.header.id        = path.sourceInfo.id;
            if (pfnGetDevInfo(&src) != ERROR_SUCCESS)
                continue;

            DISPLAYCONFIG_TARGET_DEVICE_NAME_LOCAL tgt = {};
            tgt.header.type      = DCDI_GET_TARGET_NAME;
            tgt.header.size      = sizeof(tgt);
            tgt.header.adapterId = path.targetInfo.adapterId;
            tgt.header.id        = path.targetInfo.id;
            if (pfnGetDevInfo(&tgt) != ERROR_SUCCESS)
                continue;

            MonitorInfo info;
            if (tgt.monitorFriendlyDeviceName[0] != L'\0')
            {
                char buf[128] = {};
                WideCharToMultiByte(CP_ACP, 0, tgt.monitorFriendlyDeviceName, -1,
                                    buf, sizeof(buf) - 1, nullptr, nullptr);
                info.friendly_name = buf;
            }
            else
            {
                // DisplayConfig doesn't have a friendly name (common for
                // internal laptop panels).  Fall back to the PnP
                // "Device description" so the user sees e.g.
                // "Dell XPS 15 LGD06B3 Display" instead of "\\.\DISPLAY1".
                char buf[128] = {};
                if (TryGetDeviceDesc(tgt.monitorDevicePath, buf, sizeof(buf)))
                    info.friendly_name = buf;
            }
            info.connector_instance = tgt.connectorInstance;
            info.output_technology  = tgt.outputTechnology;

            char gdi[64] = {};
            WideCharToMultiByte(CP_ACP, 0, src.viewGdiDeviceName, -1,
                                gdi, sizeof(gdi) - 1, nullptr, nullptr);
            out[gdi] = info;
        }
    }

    xr_string MakeLabel(const MonitorInfo& info, const char* gdi_fallback, bool suffix_needed)
    {
        const xr_string& base = info.friendly_name.empty()
                                    ? xr_string(gdi_fallback)
                                    : info.friendly_name;
        if (!suffix_needed)
            return base;

        char suffix[32] = {};
        const char* tech_str = ConnectorTechString(info.output_technology);
        if (tech_str)
            xr_sprintf(suffix, sizeof(suffix), " (%s-%u)", tech_str, info.connector_instance + 1);
        else
            xr_sprintf(suffix, sizeof(suffix), " (OUT-%u)", info.connector_instance + 1);

        return base + suffix;
    }

    struct OutputEntry
    {
        xr_string gdi_name;
        xr_string base_label;
        HMONITOR  hmon;
    };

    struct EnumCtx { xr_vector<OutputEntry>* outputs; GdiToInfoMap* friendly_map; };

    static BOOL CALLBACK EnumMonitorsProc(HMONITOR hMon, HDC, LPRECT, LPARAM lParam)
    {
        auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
        MONITORINFOEXA mi;
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoA(hMon, &mi))
            return TRUE;

        OutputEntry e;
        e.gdi_name = mi.szDevice;
        e.hmon     = hMon;

        auto it = ctx->friendly_map->find(e.gdi_name);
        e.base_label = (it != ctx->friendly_map->end() && !it->second.friendly_name.empty())
                           ? it->second.friendly_name
                           : xr_string(e.gdi_name);
        ctx->outputs->push_back(e);
        return TRUE;
    }

}


void fill_vid_monitor_list()
{
    if (vid_monitor_token != nullptr)
        return;

    GdiToInfoMap friendly_map;
    BuildFriendlyNameMap(friendly_map);

    xr_vector<OutputEntry> outputs;
    {
        EnumCtx ctx{ &outputs, &friendly_map };
        if (!EnumDisplayMonitors(NULL, NULL, EnumMonitorsProc, reinterpret_cast<LPARAM>(&ctx)) || outputs.empty())
            Msg("! vid_monitor: EnumDisplayMonitors returned no monitors, list will be Auto-only");
    }

    xr_map<xr_string, u32> label_count;
    for (const auto& e : outputs)
        label_count[e.base_label]++;

    u32 N = outputs.size();
    vid_monitor_token = xr_alloc<xr_token>(N + 2);

    vid_monitor_token[0].name = xr_strdup("Auto");
    vid_monitor_token[0].id   = 0;
    s_monitor_handles.push_back(NULL);

    for (u32 i = 0; i < N; ++i)
    {
        const OutputEntry& e = outputs[i];
        bool needs_suffix = (label_count[e.base_label] > 1);

        xr_string final_label;
        auto it = friendly_map.find(e.gdi_name.c_str());
        if (it != friendly_map.end())
            final_label = MakeLabel(it->second, e.gdi_name.c_str(), needs_suffix);
        else
            final_label = e.base_label;

        vid_monitor_token[i + 1].name = xr_strdup(final_label.c_str());
        vid_monitor_token[i + 1].id   = static_cast<int>(i + 1);
        s_monitor_handles.push_back(e.hmon);
    }

    vid_monitor_token[N + 1].name = nullptr;
    vid_monitor_token[N + 1].id   = -1;

    Msg("* vid_monitor: enumerated %u monitor(s)", N);
}

void free_vid_monitor_list()
{
    if (!vid_monitor_token)
        return;

    for (int i = 0; vid_monitor_token[i].name; ++i)
        xr_free(vid_monitor_token[i].name);

    xr_free(vid_monitor_token);
    vid_monitor_token = nullptr;
    s_monitor_handles.clear();
}

HMONITOR ResolveSelectedMonitor()
{
    const xr_string& name = vid_monitor_name;

    if (name.empty() || name == "Auto")
        return NULL;

    if (!vid_monitor_token)
        return NULL;

    for (u32 i = 1; vid_monitor_token[i].name; ++i)
    {
        if (xr_strcmp(vid_monitor_token[i].name, name.c_str()) == 0)
        {
            if (i < static_cast<u32>(s_monitor_handles.size()))
                return s_monitor_handles[i];
        }
    }

    Msg("! vid_monitor: '%s' not found on this system, using Auto", name.c_str());
    return NULL;
}

void refresh_vid_monitor_list()
{
    Msg("* vid_monitor: live-refresh triggered");
    free_vid_monitor_list();
    fill_vid_monitor_list();
}
