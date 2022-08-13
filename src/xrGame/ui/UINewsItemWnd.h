#pragma once

#include "UIWindow.h"
#include "xrUIXmlParser.h"
#include "../alife_time_manager.h"
class CUIStatic;
class CUITextWnd;
struct GAME_NEWS_DATA;

class CUINewsItemWnd : public CUIWindow
{
	typedef CUIWindow inherited;

	CUITextWnd* m_UIDate;
	CUITextWnd* m_UICaption;
	CUITextWnd* m_UIText;
	CUIStatic* m_UIImage;

	// Store GAME_NEWS_DATA time
public:
	ALife::_TIME_ID receive_time = 0;

public:
	CUINewsItemWnd();
	virtual ~CUINewsItemWnd();
	void Init(CUIXml& uiXml, LPCSTR start_from);
	void Setup(GAME_NEWS_DATA& news_data);

	virtual void Update()
	{
	};
};
