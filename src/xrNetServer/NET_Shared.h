#pragma once
#ifdef XR_NETSERVER_EXPORTS
#define XRNETSERVER_API
//__declspec(dllexport)
#else
	#define XRNETSERVER_API
//__declspec(dllimport)

#ifndef _EDITOR
		#pragma comment(lib,	"xrNetServer"	)
#endif
#endif

#include "../xrCore/net_utils.h"
#include <dplay/dplay8.h>
#include "net_messages.h"


#include "net_compressor.h"

XRNETSERVER_API extern ClientID BroadcastCID;

XRNETSERVER_API extern Flags32 psNET_Flags;
XRNETSERVER_API extern int psNET_ClientUpdate;
XRNETSERVER_API extern int get_psNET_ClientUpdate();
XRNETSERVER_API extern int psNET_ClientPending;
XRNETSERVER_API extern char psNET_Name[];
XRNETSERVER_API extern int psNET_ServerUpdate;
XRNETSERVER_API extern int get_psNET_ServerUpdate();
XRNETSERVER_API extern int psNET_ServerPending;

XRNETSERVER_API extern BOOL psNET_direct_connect;

// work around for GUID symbol conflicts
#define XR_GUID(x) xrInternalGuid_ ## x

// externs
extern const GUID XR_GUID(CLSID_DirectPlay8Client);

// {DA825E1B-6830-43d7-835D-0B5AD82956A2}
extern const GUID XR_GUID(CLSID_DirectPlay8Server);

// {286F484D-375E-4458-A272-B138E2F80A6A}
extern const GUID XR_GUID(CLSID_DirectPlay8Peer);


// CLSIDs added for DirectX 9

// {FC47060E-6153-4b34-B975-8E4121EB7F3C}
extern const GUID XR_GUID(CLSID_DirectPlay8ThreadPool);

// {E4C1D9A2-CBF7-48bd-9A69-34A55E0D8941}
extern const GUID XR_GUID(CLSID_DirectPlay8NATResolver);

/****************************************************************************
 *
 * DirectPlay8 Interface IIDs
 *
 ****************************************************************************/

typedef REFIID DP8REFIID;


// {5102DACD-241B-11d3-AEA7-006097B01411}
extern const GUID XR_GUID(IID_IDirectPlay8Client);

// {5102DACE-241B-11d3-AEA7-006097B01411}
extern const GUID XR_GUID(IID_IDirectPlay8Server);

// {5102DACF-241B-11d3-AEA7-006097B01411}
extern const GUID XR_GUID(IID_IDirectPlay8Peer);


// IIDs added for DirectX 9

// {0D22EE73-4A46-4a0d-89B2-045B4D666425}
extern const GUID XR_GUID(IID_IDirectPlay8ThreadPool);

// {A9E213F2-9A60-486f-BF3B-53408B6D1CBB}
extern const GUID XR_GUID(IID_IDirectPlay8NATResolver);

// {53934290-628D-11D2-AE0F-006097B01411}
extern const GUID XR_GUID(CLSID_DP8SP_IPX);


// {6D4A3650-628D-11D2-AE0F-006097B01411}
extern const GUID XR_GUID(CLSID_DP8SP_MODEM);


// {743B5D60-628D-11D2-AE0F-006097B01411}
extern const GUID XR_GUID(CLSID_DP8SP_SERIAL);


// {EBFE7BA0-628D-11D2-AE0F-006097B01411}
extern const GUID XR_GUID(CLSID_DP8SP_TCPIP);


// Service providers added for DirectX 9


// {995513AF-3027-4b9a-956E-C772B3F78006}
extern const GUID XR_GUID(CLSID_DP8SP_BLUETOOTH);

extern const GUID XR_GUID(CLSID_DirectPlay8Address);

extern const GUID XR_GUID(IID_IDirectPlay8Address);


enum
{
	NETFLAG_MINIMIZEUPDATES = (1 << 0),
	NETFLAG_DBG_DUMPSIZE = (1 << 1),
	NETFLAG_LOG_SV_PACKETS = (1 << 2),
	NETFLAG_LOG_CL_PACKETS = (1 << 3),
};

IC u32 TimeGlobal(CTimer* timer) { return timer->GetElapsed_ms(); }
IC u32 TimerAsync(CTimer* timer) { return TimeGlobal(timer); }

class XRNETSERVER_API IClientStatistic
{
	DPN_CONNECTION_INFO ci_last;
	u32 mps_recive, mps_receive_base;
	u32 mps_send, mps_send_base;
	u32 dwBaseTime;
	CTimer* device_timer;
public:
	IClientStatistic(CTimer* timer)
	{
		ZeroMemory(this, sizeof(*this));
		device_timer = timer;
		dwBaseTime = TimeGlobal(device_timer);
	}

	void Update(DPN_CONNECTION_INFO& CI);

	IC u32 getPing() { return ci_last.dwRoundTripLatencyMS; }
	IC u32 getBPS() { return ci_last.dwThroughputBPS; }
	IC u32 getPeakBPS() { return ci_last.dwPeakThroughputBPS; }
	IC u32 getDroppedCount() { return ci_last.dwPacketsDropped; }
	IC u32 getRetriedCount() { return ci_last.dwPacketsRetried; }
	IC u32 getMPS_Receive() { return mps_recive; }
	IC u32 getMPS_Send() { return mps_send; }
	IC u32 getReceivedPerSec() { return dwBytesReceivedPerSec; }
	IC u32 getSendedPerSec() { return dwBytesSendedPerSec; }


	IC void Clear()
	{
		CTimer* timer = device_timer;
		ZeroMemory(this, sizeof(*this));
		device_timer = timer;
		dwBaseTime = TimeGlobal(device_timer);
	}

	//-----------------------------------------------------------------------
	u32 dwTimesBlocked;

	u32 dwBytesSended;
	u32 dwBytesSendedPerSec;

	u32 dwBytesReceived;
	u32 dwBytesReceivedPerSec;
};
