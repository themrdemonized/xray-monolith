#pragma once
#include "customdetector.h"

class CUIArtefactDetectorSimple;

class CSimpleDetector : public CCustomDetector
{
	typedef CCustomDetector inherited;
public:
	CSimpleDetector();
	virtual ~CSimpleDetector();

protected:
	virtual void UpdateAf();
	virtual void CreateUI();
	CUIArtefactDetectorSimple& ui();
};
