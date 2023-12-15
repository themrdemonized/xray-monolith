#pragma once
#include "UIStatic.h"
#include "..\alife_registry_wrappers.h"

struct GAME_NEWS_DATA;

class CUIPdaMsgListItem : public CUIColorAnimConrollerContainer
{
	typedef CUIColorAnimConrollerContainer inherited;
private:
	GAME_NEWS_DATA* news;
public:
	virtual ~CUIPdaMsgListItem();
	void InitPdaMsgListItem(const Fvector2& size);
	virtual void SetFont(CGameFont* pFont);

	CUIStatic UIIcon;
	CUITextWnd UITimeText;
	CUITextWnd UICaptionText;
	CUITextWnd UIMsgText;

	GAME_NEWS_DATA* addNews(GAME_NEWS_DATA* fromNews);
	GAME_NEWS_DATA* getNews();
};
