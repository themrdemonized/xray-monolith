#ifndef DetailModelH
#define DetailModelH
#pragma once

#include "IRenderDetailModel.h"

class ECORE_API CDetail : public IRender_DetailModel
{
	shared_str m_shader_name;
	shared_str m_texture_name;
public:
	void Load(IReader* S, bool create_shader = true);
	void CommitShader();
	void SuspendShader();
	void Optimize();
	virtual void Unload();

	virtual void transfer(Fmatrix& mXform, fvfVertexOut* vDest, u32 C, u16* iDest, u32 iOffset);
	virtual void transfer(Fmatrix& mXform, fvfVertexOut* vDest, u32 C, u16* iDest, u32 iOffset, float du, float dv);
	virtual ~CDetail();
};
#endif
