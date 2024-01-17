#include "stdafx.h"
#include "xrSASH.h"

#include "xr_ioconsole.h"
#include "xr_ioc_cmd.h"

xrSASH ENGINE_API g_SASH;

xrSASH::xrSASH() : m_bInited(false),
                   m_bOpenAutomate(false), m_bBenchmarkRunning(false),
                   m_bRunning(false), m_bReinitEngine(false),
                   m_bExecutingConsoleCommand(false)
{
	;
}

xrSASH::~xrSASH()
{
	VERIFY(!m_bRunning);
	VERIFY(!m_bBenchmarkRunning);
}

bool xrSASH::Init(const char* pszParam)
{
	m_bInited = true;
	xr_strcpy(m_strBenchCfgName, pszParam);
	Msg("oa:: Failed to init.");
	Msg("oa:: Running native path.");
	return false;
}

void xrSASH::MainLoop()
{
	m_bRunning = true;
	m_bReinitEngine = false;

	if (m_bOpenAutomate)
	{
		LoopOA();
	}
	else
	{
		// Native benchmarks
		LoopNative();
	}

	m_bRunning = false;
}

void xrSASH::LoopOA()
{}

void xrSASH::LoopNative()
{
	string_path in_file;
	FS.update_path(in_file, "$app_data_root$", m_strBenchCfgName);

	CInifile ini(in_file);

	IReader* R = FS.r_open(in_file);
	if (R)
	{
		FS.r_close(R);

		int test_count = ini.line_count("benchmark");
		LPCSTR test_name, t;
		shared_str test_command;

		for (int i = 0; i < test_count; ++i)
		{
			ini.r_line("benchmark", i, &test_name, &t);
			//xr_strcpy(g_sBenchmarkName, test_name);

			test_command = ini.r_string_wb("benchmark", test_name);
			u32 cmdSize = test_command.size() + 1;
			Core.Params = (char*)xr_realloc(Core.Params, cmdSize);
			xr_strcpy(Core.Params, cmdSize, test_command.c_str());
			xr_strlwr(Core.Params);

			RunBenchmark(test_name);

			// Output results
			ReportNative(test_name);
		}
	}
	else
		Msg("oa:: Native path can't find \"%s\" config file.", in_file);

	FlushLog();
}

void xrSASH::ReportNative(LPCSTR pszTestName)
{
	string_path fname;
	xr_sprintf(fname, sizeof(fname), "%s.result", pszTestName);
	FS.update_path(fname, "$app_data_root$", fname);
	CInifile res(fname, FALSE, FALSE, TRUE);

	// min/max/average
	float fMinFps = flt_max;
	float fMaxFps = flt_min;

	const u32 iWindowSize = 15;

	if (m_aFrimeTimes.size() > iWindowSize * 4)
	{
		for (u32 it = 0; it < m_aFrimeTimes.size() - iWindowSize; it++)
		{
			float fTime = 0;

			for (u32 i = 0; i < iWindowSize; ++i)
				fTime += m_aFrimeTimes[it + i];

			float fFps = iWindowSize / fTime;
			if (fFps < fMinFps) fMinFps = fFps;
			if (fFps > fMaxFps) fMaxFps = fFps;
		}
	}
	else
	{
		for (u32 it = 0; it < m_aFrimeTimes.size(); it++)
		{
			float fFps = 1.f / m_aFrimeTimes[it];
			if (fFps < fMinFps) fMinFps = fFps;
			if (fFps > fMaxFps) fMaxFps = fFps;
		}
	}

	float fTotal = 0;
	float fNumFrames = 0;
	for (u32 it = 0; it < m_aFrimeTimes.size(); it++)
	{
		string32 id;
		xr_sprintf(id, sizeof(id), "%07d", it);
		res.w_float("per_frame_stats", id, 1.f / m_aFrimeTimes[it]);
		fTotal += m_aFrimeTimes[it];
		fNumFrames += 1;
	}

	// Output statistics
	res.w_float("general", "average", fNumFrames / fTotal, "average for this run");
	res.w_float("general", "min", fMinFps, "absolute (smoothed) minimum");
	res.w_float("general", "max", fMaxFps, "absolute (smoothed) maximum");
}

void xrSASH::StartBenchmark()
{
	if (!m_bRunning) return;

	VERIFY(!m_bBenchmarkRunning);

	m_bBenchmarkRunning = true;

	if (!m_bOpenAutomate)
	{
		m_aFrimeTimes.clear();
		m_aFrimeTimes.reserve(1024);
		m_FrameTimer.Start();
	}
}

void xrSASH::DisplayFrame(float t)
{
	if (!m_bRunning) return;

	VERIFY(m_bBenchmarkRunning);

	if (!m_bOpenAutomate)
	{
		m_aFrimeTimes.push_back(m_FrameTimer.GetElapsed_sec());
		m_FrameTimer.Start();
	}
}

void xrSASH::EndBenchmark()
{
	if (!m_bRunning) return;

	VERIFY(m_bBenchmarkRunning);

	m_bBenchmarkRunning = false;
}

void InitInput();
void destroyInput();
void InitEngine();
void InitSound1();
void InitSound2();
void destroySound();
void destroyEngine();

void xrSASH::GetAllOptions()
{
	Msg("SASH:: GetAllOptions.");
	TryInitEngine();

	ReleaseEngine();
}

void xrSASH::GetCurrentOptions()
{
	Msg("SASH:: GetCurrentOptions.");
	TryInitEngine();

	ReleaseEngine();
}

void xrSASH::SetOptions()
{
	Msg("SASH:: SetOptions.");
	TryInitEngine();

	ReleaseEngine();
}

void xrSASH::GetBenchmarks()
{
	Msg("SASH:: GetBenchmarks.");
}

void Startup();

void xrSASH::RunBenchmark(LPCSTR pszName)
{
	Msg("SASH:: RunBenchmark.");

	TryInitEngine(false);

	Startup();

	m_bReinitEngine = true;
}

void xrSASH::TryInitEngine(bool bNoRun)
{
	if (m_bReinitEngine)
	{
		InitEngine();
		// It was destroyed on previous exit
		Console->Initialize();
	}

	xr_strcpy(Console->ConfigFile, "user.ltx");
	if (strstr(Core.Params, "-ltx "))
	{
		string64 c_name;
		sscanf(strstr(Core.Params, "-ltx ") + 5, "%[^ ] ", c_name);
		xr_strcpy(Console->ConfigFile, c_name);
	}

	if (strstr(Core.Params, "-r2a"))
		Console->Execute("renderer renderer_r2a");
	else if (strstr(Core.Params, "-r2"))
		Console->Execute("renderer renderer_r2");
	else
	{
		CCC_LoadCFG_custom* pTmp = xr_new<CCC_LoadCFG_custom>("renderer ");
		pTmp->Execute(Console->ConfigFile);
		if (m_bOpenAutomate)
			pTmp->Execute("SASH.ltx");
		else
			pTmp->Execute(Console->ConfigFile);
		xr_delete(pTmp);
	}

	InitInput();

	Engine.External.Initialize();

	if (bNoRun)
		InitSound1();

	Console->Execute("unbindall");
	Console->ExecuteScript(Console->ConfigFile);
	if (m_bOpenAutomate)
	{
		// Overwrite setting using SASH.ltx if has any.
		xr_strcpy(Console->ConfigFile, "SASH.ltx");
		Console->ExecuteScript(Console->ConfigFile);
	}

	if (bNoRun)
	{
		InitSound2();
		Device.Create();
	}
}

void xrSASH::ReleaseEngine()
{
	m_bReinitEngine = true;

	destroyInput();
	Console->Destroy();
	destroySound();
	destroyEngine();
}

void xrSASH::OnConsoleInvalidSyntax(bool bLastLine, const char* pszMsg, ...)
{
	if (m_bInited && m_bExecutingConsoleCommand)
	{
		va_list mark;
		va_start(mark, pszMsg);
		va_end(mark);
	}
}
