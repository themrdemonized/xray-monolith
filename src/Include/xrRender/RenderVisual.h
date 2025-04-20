#ifndef	RenderVisual_included
#define	RenderVisual_included
#pragma once

class IKinematics;
class IKinematicsAnimated;
class IParticleCustom;
struct vis_data;

enum IRenderVisualFlags
{
	eIgnoreOptimization = (1 << 0),
	eNoShadow = (1 << 1),
};

class IRenderVisual
{
public:
	IRenderVisual() { flags.zero(); }

	virtual ~IRenderVisual()
	{
	}

	virtual vis_data& _BCL getVisData() = 0;
	virtual u32 getType() = 0;

	Flags16 flags;

#ifdef DEBUG
	virtual shared_str	_BCL	getDebugName() = 0;
#endif
	virtual LPCSTR _BCL getDebugShader() { return nullptr; }
	virtual LPCSTR _BCL getDebugTexture() { return nullptr; }
	
	virtual LPCSTR _BCL getDebugShaderDef() { return nullptr; }
	virtual LPCSTR _BCL getDebugTextureDef() { return nullptr; }

	virtual xr_vector<IRenderVisual*>* get_children() { return nullptr; };

	virtual void SetShaderTexture(LPCSTR shader, LPCSTR texture) {};
	virtual void ResetShaderTexture() {};
	virtual void MarkAsHot(bool is_hot) {};				//--DSR-- HeatVision
	virtual void MarkAsGlowing(bool is_glowing) {};		//--DSR-- SilencerOverheat

	virtual IRenderVisual* _BCL dcast_RenderVisual() { return this; }
	virtual IKinematics* _BCL dcast_PKinematics() { return 0; }
	virtual IKinematicsAnimated* dcast_PKinematicsAnimated() { return 0; }
	virtual IParticleCustom* dcast_ParticleCustom() { return 0; }
};

#endif	//	RenderVisual_included
