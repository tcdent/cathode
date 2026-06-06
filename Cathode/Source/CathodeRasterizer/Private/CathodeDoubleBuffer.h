#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

#include "CathodeRaster.h" // IRasterBackend, IRasterBitmap, EPixelFormat

namespace Cathode
{

// Double-buffer bookkeeping for one surface — OUR logic, on top of the swap boundary. Two CPU bitmaps
// (front + back) created through the injected backend at the surface's native dimensions/format (the
// "rasterize at native resolution, never upscale" rule). The Rasterizer draws into the BACK buffer;
// Texture Egress reads the FRONT. A swap publishes the back buffer to the front — and only happens when
// the back buffer actually changed this frame, so idle surfaces produce no frame-ready.
//
// Bespoke + testable via a RECORDING backend (no Blend2D, no pixels): swap toggles which buffer is the
// draw target, draw always targets the back, frame-ready fires only on real change, dimensions equal the
// surface.
class FDoubleBuffer
{
public:
	// Allocates both buffers via the backend at Width x Height in Format. (BufferCount is fixed at 2 per
	// the decided double-buffer config; the param documents intent and guards a future triple option.)
	FDoubleBuffer(IRasterBackend& Backend, int32 InWidth, int32 InHeight, EPixelFormat InFormat);

	/** True once both buffers allocated and valid. */
	bool IsValid() const;

	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }
	EPixelFormat GetFormat() const { return Format; }

	/** The draw target — the Rasterizer paints here. */
	IRasterBitmap* GetBackBuffer() const { return Buffers[BackIndex].Get(); }

	/** The publish/read target — Texture Egress uploads from here. */
	IRasterBitmap* GetFrontBuffer() const { return Buffers[1 - BackIndex].Get(); }

	/** Mark that the back buffer was drawn into this frame (a real change occurred). */
	void MarkChanged() { bChanged = true; }

	bool HasChanged() const { return bChanged; }

	/** Publish the back buffer to the front by swapping, IFF it changed. Returns true if a swap occurred
	 *  (=> emit frame-ready) and clears the changed flag; false (no swap) when nothing changed. */
	bool SwapIfChanged();

private:
	TUniquePtr<IRasterBitmap> Buffers[2];
	int32 BackIndex = 0; // index of the back (draw) buffer; front = 1 - BackIndex
	bool bChanged = false;
	int32 Width = 0;
	int32 Height = 0;
	EPixelFormat Format = EPixelFormat::PremultipliedBGRA8;
};

} // namespace Cathode
