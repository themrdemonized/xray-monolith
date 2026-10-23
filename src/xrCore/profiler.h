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