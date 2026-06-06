#include "CathodeDoubleBuffer.h"

namespace Cathode
{

FDoubleBuffer::FDoubleBuffer(IRasterBackend& Backend, int32 InWidth, int32 InHeight, EPixelFormat InFormat)
	: Width(InWidth)
	, Height(InHeight)
	, Format(InFormat)
{
	// Both buffers at the surface's native dimensions/format (rasterize at native, never upscale).
	Buffers[0] = Backend.CreateBitmap(InWidth, InHeight, InFormat);
	Buffers[1] = Backend.CreateBitmap(InWidth, InHeight, InFormat);
}

bool FDoubleBuffer::IsValid() const
{
	return Buffers[0].IsValid() && Buffers[0]->IsValid()
		&& Buffers[1].IsValid() && Buffers[1]->IsValid();
}

bool FDoubleBuffer::SwapIfChanged()
{
	if (!bChanged)
	{
		return false; // nothing drawn -> no publish, no frame-ready (idle surfaces cost nothing)
	}

	BackIndex = 1 - BackIndex; // publish: the drawn buffer becomes the front, the old front the new back
	bChanged = false;
	return true;
}

} // namespace Cathode
