#include "stdafx.h"
#pragma hdrstop

#include "r_backend_xform.h"

void R_xforms::set_W(const Fmatrix& m)
{
	m_w.set(m);
	m_wv.mul_43(m_v, m_w);
	m_wvp.mul(m_p, m_wv);
	if (c_w) RCache.set_c(c_w, m_w);
	if (c_wv) RCache.set_c(c_wv, m_wv);
	if (c_wvp) RCache.set_c(c_wvp, m_wvp);
	m_bInvWValid = false;
	if (c_invw) apply_invw();
	RCache.set_xform(D3DTS_WORLD, m);
}

void R_xforms::set_V(const Fmatrix& m)
{
	m_v.set(m);
	m_wv.mul_43(m_v, m_w);
	m_vp.mul(m_p, m_v);
	m_wvp.mul(m_p, m_wv);
	if (c_v) RCache.set_c(c_v, m_v);
	if (c_vp) RCache.set_c(c_vp, m_vp);
	if (c_wv) RCache.set_c(c_wv, m_wv);
	if (c_wvp) RCache.set_c(c_wvp, m_wvp);
	RCache.set_xform(D3DTS_VIEW, m);
}

void R_xforms::set_P(const Fmatrix& m)
{
	m_p.set(m);
	m_vp.mul(m_p, m_v);
	m_wvp.mul(m_p, m_wv);
	if (c_p) RCache.set_c(c_p, m_p);
	if (c_vp) RCache.set_c(c_vp, m_vp);
	if (c_wvp) RCache.set_c(c_wvp, m_wvp);
	// always setup projection - D3D relies on it to work correctly :(
	RCache.set_xform(D3DTS_PROJECTION, m);
}

void R_xforms::set_W_prev(const Fmatrix& m)
{
	const bool svp = Device.m_SecondViewport.IsSVPFrame(); // pip pick this viewport's prev-matrix slot
	m_w_prev[svp].set(m);
	m_wv_prev[svp].mul_43(m_v_prev[svp], m_w_prev[svp]);
	m_wvp_prev[svp].mul(m_p_prev[svp], m_wv_prev[svp]);

	if (c_w_prev[svp])		RCache.set_c(c_w_prev[svp], m_w_prev[svp]);
	if (c_wv_prev[svp])		RCache.set_c(c_wv_prev[svp], m_wv_prev[svp]);
	if (c_wvp_prev[svp])	RCache.set_c(c_wvp_prev[svp], m_wvp_prev[svp]);
}
void R_xforms::set_V_prev(const Fmatrix& m)
{
	const bool svp = Device.m_SecondViewport.IsSVPFrame();
	m_v_prev[svp].set(m);
	m_wv_prev[svp].mul_43(m_v_prev[svp], m_w_prev[svp]);
	m_vp_prev[svp].mul(m_p_prev[svp], m_v_prev[svp]);
	m_wvp_prev[svp].mul(m_p_prev[svp], m_wv_prev[svp]);

	if (c_v_prev[svp])		RCache.set_c(c_v_prev[svp], m_v_prev[svp]);
	if (c_vp_prev[svp])		RCache.set_c(c_vp_prev[svp], m_vp_prev[svp]);
	if (c_wv_prev[svp])		RCache.set_c(c_wv_prev[svp], m_wv_prev[svp]);
	if (c_wvp_prev[svp])	RCache.set_c(c_wvp_prev[svp], m_wvp_prev[svp]);
}
void R_xforms::set_P_prev(const Fmatrix& m)
{
	const bool svp = Device.m_SecondViewport.IsSVPFrame();
	m_p_prev[svp].set(m);
	m_vp_prev[svp].mul(m_p_prev[svp], m_v_prev[svp]);
	m_wvp_prev[svp].mul(m_p_prev[svp], m_wv_prev[svp]);
	if (c_p_prev[svp])		RCache.set_c(c_p_prev[svp], m_p_prev[svp]);
	if (c_vp_prev[svp])		RCache.set_c(c_vp_prev[svp], m_vp_prev[svp]);
	if (c_wvp_prev[svp])	RCache.set_c(c_wvp_prev[svp], m_wvp_prev[svp]);
}

void R_xforms::apply_invw()
{
	VERIFY(c_invw);

	if (!m_bInvWValid)
	{
		m_invw.invert_b(m_w);
		m_bInvWValid = true;
	}

	RCache.set_c(c_invw, m_invw);
}

void R_xforms::unmap()
{
	c_w = NULL;
	c_invw = NULL;
	c_v = NULL;
	c_p = NULL;
	c_wv = NULL;
	c_vp = NULL;
	c_wvp = NULL;

	for (int i = 0; i < 2; i++)
	{
		c_w_prev[i] = NULL;
		c_v_prev[i] = NULL;
		c_p_prev[i] = NULL;
		c_wv_prev[i] = NULL;
		c_vp_prev[i] = NULL;
		c_wvp_prev[i] = NULL;
	}
}

R_xforms::R_xforms()
{
	unmap();
	m_w.identity();
	m_invw.identity();
	m_v.identity();
	m_p.identity();
	m_wv.identity();
	m_vp.identity();
	m_wvp.identity();

	for (int i = 0; i < 2; i++)
	{
		m_w_prev[i].identity();
		m_v_prev[i].identity();
		m_p_prev[i].identity();
		m_wv_prev[i].identity();
		m_vp_prev[i].identity();
		m_wvp_prev[i].identity();
	}

	m_bInvWValid = true;
}
