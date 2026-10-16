#pragma once


class IDamageSource
{
public:
	virtual ~IDamageSource()
	{
	} ;
	virtual void SetInitiator(u32 id) =0;
	virtual u32 Initiator() =0;
	virtual IDamageSource* cast_IDamageSource() =0; //{return this	;}
};
