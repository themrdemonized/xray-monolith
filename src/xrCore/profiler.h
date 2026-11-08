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
#	include <cstdio>
#	include <mutex>
#	include <string>
#	include <unordered_map>

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

		// Repeated names get a number, a new thread with a dead thread's name and id would reuse its Optick entry
		static std::mutex lock;
		static std::unordered_map<std::string, unsigned> uses;
		unsigned n;
		{
			std::lock_guard<std::mutex> guard(lock);
			n = ++uses[name];
		}
		if (n == 1)
		{
			::Optick::RegisterThread(name);
			return;
		}
		char numbered[256];
		snprintf(numbered, sizeof(numbered), "%s %u", name, n);
		::Optick::RegisterThread(numbered);
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
#	define PROF_EVENT(...) OPTICK_EVENT(__VA_ARGS__)
// MSVC sign-extends Optick's colour over the filter bits, so the mask is rebuilt from the filter
#	define PROF_CAT(X) ::Optick::Category::Type((1ull << (::Optick::Filter::X + 32)) | uint32_t(::Optick::Category::X))
#	define PROF_TAG(Name, Value) OPTICK_TAG(Name, Value)
#	define START_PROFILE(...) { PROF_EVENT(__VA_ARGS__)
#	define STOP_PROFILE		}
#else
#	define START_PROFILE(...) {
#	define STOP_PROFILE		}

#	define PROF_THREAD(Name)
#	define PROF_START_CAPTURE()
#	define PROF_STOP_CAPTURE()
#	define PROF_SAVE_CAPTURE(Name)
#	define PROF_FRAME(Name)
#	define PROF_EVENT(...)
#	define PROF_CAT(X)
#	define PROF_TAG(Name, Value)
#	define PROF_EVENT_DYNAMIC(...) {};
#endif