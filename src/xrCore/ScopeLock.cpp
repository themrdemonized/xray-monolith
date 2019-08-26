#include "stdafx.h"
#include "ScopeLock.hpp"
#include "Lock.hpp"
#include "xrDebug.h"

ScopeLock::ScopeLock(Lock* SyncObject) : syncObject(SyncObject)
{
	R_ASSERT(syncObject);
	syncObject->Enter();
}

ScopeLock::~ScopeLock()
{
	syncObject->Leave();
}