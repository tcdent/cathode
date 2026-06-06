#pragma once

#include "CoreMinimal.h"

#include "CathodeLayout.h" // ITextMeasurer, FLayoutFontRef

namespace Cathode
{

// The font-handle cache the document_container delegates create_font / text_width to — OUR bespoke
// bookkeeping, litehtml-free. litehtml's create_font returns an opaque uint_ptr handle and later passes
// it to text_width/draw_text; this maps that handle <-> our FLayoutFontRef and DEDUPES identical font
// requests (same face/size/weight/italic -> the same handle, one ResolveFont). Measurement dispatches
// through it to the neutral ITextMeasurer. Testable via a recording measurer; no litehtml.
class FLayoutFontCache
{
public:
	explicit FLayoutFontCache(ITextMeasurer& InMeasurer);

	/** Resolve a font to a stable handle (the uint_ptr litehtml stores). Same params -> same handle, and
	 *  the underlying ITextMeasurer::ResolveFont is called only once per distinct font. 0 = invalid. */
	uint64 ResolveFont(const FString& Face, int32 PixelSize, int32 Weight, bool bItalic);

	/** litehtml delete_font — the cache retains resolved fonts (no-op release at this altitude). */
	void ReleaseFont(uint64 Handle);

	/** Measure a text run's width via the handle's font (dispatches to ITextMeasurer). */
	float MeasureWidth(uint64 Handle, const FString& Text);

	/** Font metrics for the handle's font (dispatches to ITextMeasurer); for litehtml create_font's out. */
	void GetFontMetrics(uint64 Handle, float& OutAscent, float& OutDescent, float& OutHeight);

	/** The neutral font ref behind a handle (for emitting a DrawText op). Invalid ref if unknown. */
	FLayoutFontRef GetRef(uint64 Handle) const;

	int32 NumResolved() const { return Refs.Num(); }

private:
	ITextMeasurer& Measurer;
	TArray<FLayoutFontRef> Refs;       // handle (index + 1) -> neutral font ref
	TMap<FString, uint64> KeyToHandle; // dedup: "face|size|weight|italic" -> handle
};

} // namespace Cathode
