#include "stdafx.h"

#include "mezz_stringbuffer.h"

MezzStringBuffer::MezzStringBuffer(uint32_t Size)
{
	StringBuffer = std::make_unique<char[]>(Size);
	BufferRaw = StringBuffer.get();

	BufferSize = Size;
}

char* MezzStringBuffer::GetBuffer() const
{
	return BufferRaw;
}

uint32_t MezzStringBuffer::GetSize() const
{
	return BufferSize;
}

MezzStringBuffer::operator char* () const
{
	return BufferRaw;
}