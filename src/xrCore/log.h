#pragma once
#include "DateTime.hpp"
#include <atomic>
#define VPUSH(a)	((a).x), ((a).y), ((a).z)

void 	XRCORE_API		Msg	(const char* format, ...);
#include "svp_log.h" // pip PipMsg, chain-included so every consumer keeps seeing it

// Old shit
void 	XRCORE_API		Log			(const char* msg);
void 	XRCORE_API		Log			(const char* msg, const Fvector& dop);
void 	XRCORE_API		Log			(const char* msg, const Fmatrix& dop);
void 	XRCORE_API		Log			(const char* msg, const char* dop);
void 	XRCORE_API		Log			(const char* msg, u32 dop);
void 	XRCORE_API		Log			(const char* msg, int dop);
void 	XRCORE_API		Log			(const char* msg, float dop);
void 	XRCORE_API		LogWinErr(const char* msg, long err_code);

xr_string FormatString(LPCSTR fmt, ...);

class XRCORE_API xrLogger
{
public:
	using LogCallback = void(*)	(const char* string);

	void Msg(LPCSTR Msg, va_list argList);
	void SimpleMessage(LPCSTR Message, u32 MessageSize = 0);

	static void OpenLogFile();
	static const string_path& GetLogPath();
	static void EnableFastDebugLog();
	static void InitLog();
	static void FlushLog();
	static void CloseLog();
	static void SetImmediateMode(bool enable);

	static void AddLogCallback(LogCallback logCb);
	static void RemoveLogCallback(LogCallback logCb);

	void PauseLogging();
	void UnpauseLogging();

	xrLogger();
	~xrLogger();

	void LogThreadEntry();

private:
	void InternalCloseLog();
	volatile bool bIsAlive;
	ThreadID hLogThread;

	void InternalOpenLogFile();

	void InternalPrintRecord();
	void InternalPrintAllRecords();

	void InternalFlushLog();

	string_path logFileName;
	volatile IWriter* logFile;

	struct LogRecord
	{
		LogRecord() {}
		LogRecord(LPCSTR Msg, u32 sizeMsg);
		xr_string Message;
		Time time;
	};

	xrCriticalSection logDataGuard;
	xrCriticalSection logCallbackGuard;
	xrCriticalSection logFlushGuard;
	bool bFastDebugLog;

	std::atomic_bool bFlushRequested;
	bool bImmediateMode;

	//LogCallback onLogMsg;
	xr_list<LogCallback> logCallbackList;
	xr_fixedqueue<xr_string, 512> tempLogData;
public:
	static xr_queue <LogRecord>* logData;
};
