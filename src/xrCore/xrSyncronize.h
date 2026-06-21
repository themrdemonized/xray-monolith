#pragma once

#include <mutex>
#include <shared_mutex>
#include "_noncopyable.h"
#if 0//def DEBUG
# define PROFILE_CRITICAL_SECTIONS
#endif // DEBUG

#ifdef PROFILE_CRITICAL_SECTIONS
typedef void(*add_profile_portion_callback) (LPCSTR id, const u64& time);
void XRCORE_API set_add_profile_portion(add_profile_portion_callback callback);

# define STRINGIZER_HELPER(a) #a
# define STRINGIZER(a) STRINGIZER_HELPER(a)
# define CONCATENIZE_HELPER(a,b) a##b
# define CONCATENIZE(a,b) CONCATENIZE_HELPER(a,b)
# define MUTEX_PROFILE_PREFIX_ID #mutexes/
# define MUTEX_PROFILE_ID(a) STRINGIZER(CONCATENIZE(MUTEX_PROFILE_PREFIX_ID,a))
#endif // PROFILE_CRITICAL_SECTIONS

// Desc: Simple wrapper for critical section
class XRCORE_API xrCriticalSection : xray::noncopyable
{
public:
	class XRCORE_API raii
	{
	public:
		raii(xrCriticalSection*);
		~raii();

	private:
		xrCriticalSection* critical_section;
	};

private:
	void* pmutex;
#ifdef PROFILE_CRITICAL_SECTIONS
    LPCSTR m_id;
#endif // PROFILE_CRITICAL_SECTIONS

public:
#ifdef PROFILE_CRITICAL_SECTIONS
    xrCriticalSection(LPCSTR id);
#else // PROFILE_CRITICAL_SECTIONS
	xrCriticalSection();
#endif // PROFILE_CRITICAL_SECTIONS
	~xrCriticalSection();

	void Enter();
	void Leave();
	BOOL TryEnter();

	bool IsValid() { return pmutex != nullptr; }
};

class xrCriticalSectionGuard : xray::noncopyable
{
private:
	xrCriticalSection* critical_section;

public:
	void Enter()
	{
		critical_section->Enter();
	}
	void Leave()
	{
		critical_section->Leave();
	}
	xrCriticalSectionGuard(xrCriticalSection* cs) : critical_section(cs) { Enter(); }
	xrCriticalSectionGuard(xrCriticalSection& cs) : critical_section(&cs) { Enter(); }

	~xrCriticalSectionGuard() { Leave(); }
};

// Non-blocking variant: tries to acquire the lock and never blocks. If another thread already
// holds it, owns_lock() is false and the section is left unguarded. Use where a blocking acquire
// could deadlock (e.g. taking a second object's lock while holding one) and proceeding without
// the lock is acceptable.
class xrCriticalSectionTryGuard : xray::noncopyable
{
private:
	xrCriticalSection* critical_section;
	bool owned;

public:
	xrCriticalSectionTryGuard(xrCriticalSection* cs) : critical_section(cs), owned(cs->TryEnter() != FALSE) {}
	xrCriticalSectionTryGuard(xrCriticalSection& cs) : critical_section(&cs), owned(cs.TryEnter() != FALSE) {}

	bool owns_lock() const { return owned; }

	~xrCriticalSectionTryGuard() { if (owned) critical_section->Leave(); }
};

using ThreadID = HANDLE;


class XRCORE_API xrSRWLock
{
private:
    SRWLOCK smutex;

public:
    xrSRWLock();
    ~xrSRWLock() {};

    void AcquireExclusive();
    void ReleaseExclusive();

    void AcquireShared();
    void ReleaseShared();

    BOOL TryAcquireExclusive();
    BOOL TryAcquireShared();
};
//Write functions guard: lock.AcquireExclusive(); ... lock.ReleaseExclusive();
//Read functions guard: lock.AcquireShared(); ... lock.ReleaseShared();

class XRCORE_API xrSRWLockGuard
{
public:
    xrSRWLockGuard(xrSRWLock& lock, bool shared = false);
    xrSRWLockGuard(xrSRWLock* lock, bool shared = false);
    ~xrSRWLockGuard();

private:
    xrSRWLock* lock;
    bool shared;
};
//Write functions guard: xrSRWLockGuard guard(lock); ...
//Read functions guard: xrSRWLockGuard guard(lock, true); ...
