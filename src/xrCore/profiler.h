#pragma once

// Profiler backends
#define PROFILER_NONE   (0)
#define PROFILER_OPTICK (1)

// Set active profiler backend
#if !defined(XRCORE_PROFILER)
    #define XRCORE_PROFILER PROFILER_NONE
//  #define XRCORE_PROFILER PROFILER_OPTICK
#endif

// Implement profiler macro interface
#if XRCORE_PROFILER == PROFILER_OPTICK
#	include "../3rd party/optick-git/src/optick.h"
#	include <atomic>
#	include <cstdio>

namespace xrProfiler
{
	// Whether this thread has its name in the profiler, and whether it is dropped when the thread exits
	struct ThreadRecord
	{
		bool named = false;
		bool owned = false;
		~ThreadRecord()
		{
			if (owned)
				::Optick::UnRegisterThread(false);
		}
	};

	inline ThreadRecord& thread_record()
	{
		static thread_local ThreadRecord record;
		return record;
	}

	// Threads keep the first name they get
	inline void RegisterThread(const char* name)
	{
		ThreadRecord& r = thread_record();
		if (r.named)
			return;
		r.named = true;
		r.owned = true;
		::Optick::RegisterThread(name);
	}

	// Pool workers take a numbered name before the first engine task they run
	inline void RegisterWorker()
	{
		if (thread_record().named)
			return;
		static std::atomic<unsigned> count = 0;
		char name[32];
		snprintf(name, sizeof(name), "PPL Worker %u", ++count);
		RegisterThread(name);
	}

	// Optick registers the frame thread itself
	inline void MainThread()
	{
		thread_record().named = true;
	}

}

#	define PROF_THREAD(Name) ::xrProfiler::RegisterThread(Name);
#	define PROF_START_CAPTURE() OPTICK_START_CAPTURE()
#	define PROF_STOP_CAPTURE() OPTICK_STOP_CAPTURE()
#	define PROF_SAVE_CAPTURE(Name) OPTICK_SAVE_CAPTURE(Name)
#	define PROF_FRAME(Name) ::xrProfiler::MainThread(); OPTICK_FRAME(Name)
#	define PROF_EVENT(Name) OPTICK_EVENT(Name)
#	define PROF_TAG(Name, Value) OPTICK_TAG(Name, Value)
#	define PROF_EVENT_DYNAMIC(...)	 static ::Optick::EventDescription* OPTICK_CONCAT(autogen_description_, __LINE__) = nullptr; \
							 OPTICK_CONCAT(autogen_description_, __LINE__) = ::Optick::CreateDescription(OPTICK_FUNC, __FILE__, __LINE__, ##__VA_ARGS__); \
							 ::Optick::Event OPTICK_CONCAT(autogen_event_, __LINE__)( *(OPTICK_CONCAT(autogen_description_, __LINE__)) ); 
#	define START_PROFILE(a) { PROF_EVENT(a)
#	define STOP_PROFILE		}
#else
#	define START_PROFILE(a) {
#	define STOP_PROFILE		}

#	define PROF_THREAD(Name)
#	define PROF_START_CAPTURE()
#	define PROF_STOP_CAPTURE()
#	define PROF_SAVE_CAPTURE(Name)
#	define PROF_FRAME(Name)
#	define PROF_EVENT(Name)
#	define PROF_TAG(Name, Value)
#	define PROF_EVENT_DYNAMIC(...) {};
#endif