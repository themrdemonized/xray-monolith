#pragma once


class IDamageSource
{
public:
	virtual void SetInitiator(ALife::_OBJECT_ID id) =0;
	virtual ALife::_OBJECT_ID Initiator() =0;
	virtual IDamageSource* cast_IDamageSource() { return this; }
};
