#pragma once
#include "UIProgressBar.h"
#include "UIProgressShape.h"

class CUIMotionIcon : public CUIWindow
{
	typedef CUIWindow inherited;
public:
private:
	CUIProgressShape m_luminosity_progress;
	CUIProgressShape m_noise_progress;


public:
	virtual ~CUIMotionIcon();
	CUIMotionIcon();
	virtual void Update();
	virtual void Draw();
	void Init(Frect const& rect);
	void SetNoise(float Pos);
};
