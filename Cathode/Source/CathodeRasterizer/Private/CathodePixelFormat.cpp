#include "CathodePixelFormat.h"

namespace Cathode
{

int32 PixelFormatBytesPerPixel(EPixelFormat Format)
{
	switch (Format)
	{
	case EPixelFormat::PremultipliedBGRA8:
	case EPixelFormat::OpaqueBGRA8:
		return 4;
	case EPixelFormat::Alpha8:
		return 1;
	default:
		return 0;
	}
}

int32 MinRowStride(int32 Width, EPixelFormat Format)
{
	if (Width <= 0)
	{
		return 0;
	}
	return Width * PixelFormatBytesPerPixel(Format);
}

int64 MinBufferSize(int32 Width, int32 Height, EPixelFormat Format)
{
	if (Width <= 0 || Height <= 0)
	{
		return 0;
	}
	return static_cast<int64>(MinRowStride(Width, Format)) * static_cast<int64>(Height);
}

} // namespace Cathode
