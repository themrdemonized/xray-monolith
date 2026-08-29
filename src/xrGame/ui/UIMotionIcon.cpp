#include "stdafx.h"
#include "UIMainIngameWnd.h"
#include "UIMotionIcon.h"
#include "UIXmlInit.h"
#include "../actor.cpp"

const LPCSTR MOTION_ICON_XML = "motion_icon.xml";

CUIMotionIcon::CUIMotionIcon()
{
}

CUIMotionIcon::~CUIMotionIcon()
{
}

void CUIMotionIcon::Init(Frect const& zonemap_rect)
{
	CUIXml uiXml;
	uiXml.Load(CONFIG_PATH, UI_PATH, MOTION_ICON_XML);

	CUIXmlInit xml_init;

	xml_init.InitWindow(uiXml, "window", 0, this);
	float rel_sz = uiXml.ReadAttribFlt("window", 0, "rel_size", 1.0f);
	Fvector2 sz;
	Fvector2 pos;
	zonemap_rect.getsize(sz);

	pos.set(sz.x / 2.0f, sz.y / 2.0f);
	SetWndSize(sz);
	SetWndPos(pos);

	float k = UI().get_current_kx();
	sz.mul(rel_sz * k);

	AttachChild(&m_luminosity_progress);
	xml_init.InitProgressShape(uiXml, "luminosity_progress", 0, &m_luminosity_progress);
	m_luminosity_progress.SetWndSize(sz);
	m_luminosity_progress.SetWndPos(pos);

	AttachChild(&m_noise_progress);
	xml_init.InitProgressShape(uiXml, "noise_progress", 0, &m_noise_progress);
	m_noise_progress.SetWndSize(sz);
	m_noise_progress.SetWndPos(pos);
}

void CUIMotionIcon::SetNoise(float pos)
{
	if (!IsGameTypeSingle())
		return;

	if (!IsShown())
		return;

	pos = clampr(pos, 0.f, 100.f);
	m_noise_progress.SetPos(pos / 100.f);
}

void CUIMotionIcon::Draw()
{
	if (!IsShown())
		return;

	inherited::Draw();
}

void CUIMotionIcon::Update()
{
	if (!IsGameTypeSingle())
	{
		inherited::Update();
		return;
	}

	if (!IsShown())
		return;

	inherited::Update();

    float cur_vis = GetActorVisibility();
    m_luminosity_progress.SetPos(cur_vis);
}
