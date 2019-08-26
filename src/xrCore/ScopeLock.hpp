#pragma once

#include "Lock.hpp"

class Lock;

class ScopeLock : Noncopyable
{
	Lock* syncObject;

public:
	ScopeLock(Lock* SyncObject);
	~ScopeLock();
};
