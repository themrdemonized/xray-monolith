////////////////////////////////////////////////////////////////////////////
//	Module 		: object_factory.cpp
//	Created 	: 27.05.2004
//  Modified 	: 27.05.2004
//	Author		: Dmitriy Iassenev
//	Description : Object factory
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "object_factory.h"
#include "object_broker.h"

CObjectFactory* g_object_factory = 0;

static void FactoryDiagWrite(const char* msg) {
	HANDLE h = CreateFileA("D:\\anomaly\\appdata\\logs\\vulkan_diag.txt",
		FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		DWORD written;
		WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
		WriteFile(h, "\r\n", 2, &written, NULL);
		FlushFileBuffers(h);
		CloseHandle(h);
	}
}

CObjectFactory::CObjectFactory()
{
	FactoryDiagWrite("[DIAG] CObjectFactory::ctor: ENTER");
	m_actual = false;
	FactoryDiagWrite("[DIAG] CObjectFactory::ctor: before register_classes");
	register_classes();
	FactoryDiagWrite("[DIAG] CObjectFactory::ctor: after register_classes");
}

CObjectFactory::~CObjectFactory()
{
	delete_data(m_clsids);
}

void CObjectFactory::init()
{
	FactoryDiagWrite("[DIAG] CObjectFactory::init: before register_script_classes");
	register_script_classes();
	FactoryDiagWrite("[DIAG] CObjectFactory::init: after register_script_classes");
}
