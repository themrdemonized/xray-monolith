#pragma once

#include "CustomDevice.h"

class CUIDosimeter;

class CDosimeter : public CCustomDevice
{
	typedef CCustomDevice inherited;
public:

	void render_item_3d_ui();

protected:
	void CreateUI();
	CUIDosimeter& ui();
};
