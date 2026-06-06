#pragma once

#include "CoreMinimal.h"

#include "CathodeRaster.h" // EPixelFormat

namespace Cathode
{

// Pixel-format math — OUR bookkeeping, independent of the backend. The backend computes the actual
// bitmap stride (it may pad rows for alignment); these helpers give the format's bytes-per-pixel and the
// MINIMUM row stride / buffer size we expect, used to validate and reason about the bitmap the backend
// hands back. Pure functions; unit-tested directly (no pixels, no Blend2D).

/** Bytes per pixel for a format. */
int32 PixelFormatBytesPerPixel(EPixelFormat Format);

/** Minimum bytes per row (Width * bytesPerPixel) — the backend's actual stride is >= this. */
int32 MinRowStride(int32 Width, EPixelFormat Format);

/** Minimum buffer size in bytes for a tightly-packed bitmap (no row padding). */
int64 MinBufferSize(int32 Width, int32 Height, EPixelFormat Format);

} // namespace Cathode
