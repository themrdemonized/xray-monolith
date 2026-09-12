#pragma once

template <class T>
IC void dx10_set_debug_name(T* resource, const char* name)
{
	if (!resource || !name || !name[0])
		return;

	// WKPDID_D3DDebugObjectName, spelled out so no dxguid import is needed
	static const GUID debug_object_name =
		{0x429b8c22, 0x9188, 0x4b0c, {0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00}};

	resource->SetPrivateData(debug_object_name, UINT(xr_strlen(name)), name);
}
