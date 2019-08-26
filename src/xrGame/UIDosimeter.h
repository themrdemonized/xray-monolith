#pragma once

#include "ui/ArtefactDetectorUI.h"
#include "Dosimeter.h"

class CUIDosimeter : public CUICustomDeviceBase, public CUIWindow
{
	typedef CUICustomDeviceBase inherited;
public:
	void update() override;
	void Draw() override;

	void construct(CDosimeter* p);

private:
	CUIStatic* m_wrk_area;
	CUIStatic* m_seg1;
	CUIStatic* m_seg2;
	CUIStatic* m_seg3;
	CUIStatic* m_seg4;
	CDosimeter* m_parent;
	Fmatrix m_map_attach_offset;

	void GetUILocatorMatrix(Fmatrix& _m);

	// Ďđčçíŕę đŕáîňű ďđčáîđŕ: ěčăŕţůŕ˙ ňî÷ęŕ â ďđŕâîě íčćíĺě óăëó
	CUIStatic* m_workIndicator;
	const u32 WORK_PERIOD = 1000; // Ďĺđčîä ěčăŕíč˙ číäčęŕňîđŕ
	u32 m_workTick; // Âđĺě˙ ďĺđĺęëţ÷ĺíč˙ číäčęŕňîđŕ

	// Ýěóë˙öč˙ řóěŕ ďđč čçěĺđĺíčč: ěëŕäřčé đŕçđ˙ä ěĺí˙ĺňń˙ â ďđĺäĺë˙ő 8 ĺäčíčö
	float m_noise; // Âĺëč÷číŕ řóěŕ
	const u32 NOISE_PERIOD = 3000; // Ďĺđčîä ďĺđĺđŕń÷ĺňŕ řóěŕ
	u32 m_noiseTick; // Âđĺě˙ ďîńëĺäíĺăî ďĺđĺđŕń÷ĺňŕ řóěŕ
};
