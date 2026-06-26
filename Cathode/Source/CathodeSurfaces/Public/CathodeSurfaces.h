#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

// Cathode Surfaces — the per-surface WORLD-OBJECT layer and the many-surfaces model: one shared runtime
// driving many independent displays (wall dashboards down to single-character LCDs). The FIRST pure-bespoke
// component — it wraps NO vendor. It is orchestration over the sibling components' NEUTRAL seams.
//
// 003 (Rasterizer) already built the per-surface PRODUCER (FSurfaceRasterizer owns the FDoubleBuffer +
// FDirtyRegion + emits FFrameReadySeam). Surfaces COMPOSES that producer through the neutral
// ISurfaceRenderTarget seam below — it does NOT duplicate buffering / dirty-tracking / frame-ready, and it
// never touches a CPU bitmap. What Surfaces owns is the layer 003 does not have: the surface-as-world-object
// (identity + FIXED dims/format), the runtime->surfaces registry, lifecycle, the registration API, the
// document binding (005), and the event-driven, surface-addressed, only-on-change readiness relay to Egress.
//
// ALTITUDE: the handful of ops to register a surface, drive its frame, and relay readiness — NOT a window
// manager, NOT a compositor. No resize (canvases are created with explicit dims at instantiation and never
// change — N/A by design, so the render-target seam carries NO Resize). The buffer PIXELS are the
// Rasterizer's (ultimately Blend2D's) vendor output, carried through, never interpreted or asserted by value.
//
// COUPLING: Surfaces = Core ONLY + these neutral types. NO BL*/hb_*/litehtml, NO CathodeRasterizer/
// CathodeLayout/CathodeText/CathodeRuntime import — "couples to none" (cleaner than 005: there is no vendor
// TU at all). Integration wires ISurfaceRenderTarget -> 003's FSurfaceRasterizer, ISurfaceDocument -> 005's
// ILayoutEngine, and the readiness seam -> Texture Egress; the device-spawn caller is integration too.

namespace Cathode
{

// --- Identity + format (our neutral types; mapped to the siblings' at integration) ---

// A live surface handle — the registry key and the readiness address. A runtime handle (not a persistent
// world id); the registry assigns it. (Mirrors 005's FLayoutFontRef shape.)
struct FSurfaceId
{
	int32 Value = INDEX_NONE;
	bool IsValid() const { return Value != INDEX_NONE; }
	bool operator==(const FSurfaceId& Other) const { return Value == Other.Value; }
	friend uint32 GetTypeHash(const FSurfaceId& Id) { return ::GetTypeHash(Id.Value); }
};

// Neutral pixel format — mirrors 003's EPixelFormat; maps to it at integration. Surfaces only carries the
// format as identity (it never reads pixels). Default is the decided surface format.
enum class ESurfacePixelFormat : uint8
{
	PremultipliedBGRA8, // 32bpp premultiplied, sRGB-encoded — the surface default (-> 003 PremultipliedBGRA8)
	OpaqueBGRA8,        // 32bpp, alpha ignored
	Alpha8,             // 8bpp coverage
};

// --- 031 S2: the Cathode-OWNED input-intent vocabulary (the inversion — the host delivers SEMANTIC intents; Cathode owns the
// contract + the intent->key map). The ratified six (components-controller.md) that Input Routing delivers as synthetic
// keydown (components-input-routing.md). The HOST resolves its physical input to one of these + delivers it INTO Cathode. ---
enum class ECathodeInputIntent : uint8
{
	Up,
	Down,
	Left,
	Right,
	Accept,
	Back,
};

// The ratified intent -> canonical DOM key map (components-input-routing.md; `code` == `key` for these six). Cathode OWNS this
// — it MOVED off the host adapter in 031 S2 (resolving the 030 doc-vs-code gap where FUptimeSurfaceHandshakeAdapter held it):
// the host delivers the INTENT, Cathode maps it to the keydown. Empty for an out-of-range value (no synthetic keydown).
inline FString CathodeIntentToKey(ECathodeInputIntent Intent)
{
	switch (Intent)
	{
		case ECathodeInputIntent::Up:     return TEXT("ArrowUp");
		case ECathodeInputIntent::Down:   return TEXT("ArrowDown");
		case ECathodeInputIntent::Left:   return TEXT("ArrowLeft");
		case ECathodeInputIntent::Right:  return TEXT("ArrowRight");
		case ECathodeInputIntent::Accept: return TEXT("Enter");
		case ECathodeInputIntent::Back:   return TEXT("Escape");
	}
	return FString();
}

// --- Carries-it payloads (opaque to Surfaces; never interpreted, never asserted by value) ---

// An opaque batch of the surface's rendered paint ops, produced by its document (005) and consumed by its
// render target (003). Surfaces carries it document->target WITHOUT interpreting it (the ops are 005's
// FLayoutResult paint ops at integration). carries-it: assert identity, never contents.
struct FSurfacePaintBatch
{
	const void* Ops = nullptr;
	bool operator==(const FSurfacePaintBatch& Other) const { return Ops == Other.Ops; }
};

// A vendor-NEUTRAL view of a surface's published (front) CPU buffer — the pixel pointer + row stride + dims +
// neutral format. The producer (003 via the composition root) POPULATES it from its FBitmapData; Egress reads
// this view and copies the bytes on the event ("the engine always references the buffer"), NEVER piercing a
// vendor type (RQ2 (ii): the seam is neutralized HERE — no void* into a BL*/FBitmapData). `Pixels` is a
// BORROWED view of the producer's front buffer, valid only during the readiness callback — the 023 Handle's
// borrow contract, UNCHANGED; the async copy-at-callback that lets it cross the render-thread hop is Egress's
// (1b), not Surfaces'. Surfaces still never reads the pixels itself; the VALUES are the Rasterizer's, copied by
// Egress, never asserted by Surfaces. carries-it for identity: == by the pixel pointer (the front buffer's
// address is its identity, as the opaque Handle was).
struct FSurfaceBufferRef
{
	const void* Pixels = nullptr; // first row, top-left; the producer's front-buffer bytes (borrowed, not owned)
	int32 Stride = 0;             // bytes per row (>= Width * bytes-per-pixel; may be padded)
	int32 Width = 0;
	int32 Height = 0;
	ESurfacePixelFormat Format = ESurfacePixelFormat::PremultipliedBGRA8; // populated by the producer; first READ by Egress (1b)
	bool IsValid() const { return Pixels != nullptr; }
	bool operator==(const FSurfaceBufferRef& Other) const { return Pixels == Other.Pixels; }
};

// A surface's ready frame: the published front buffer + the dirty rectangles. Both are RECEIVED from the
// producer (003) — opaque carries-it (the rects are the producer's bookkeeping, asserted by identity/count,
// not re-derived here).
struct FSurfaceReadyFrame
{
	FSurfaceBufferRef Buffer;
	TArray<FIntRect> DirtyRects;
};

// --- The readiness relay to Texture Egress (event-driven, surface-addressed, only-on-change) ---

// The surface-addressed readiness payload Egress consumes: WHICH surface + its ready front buffer + dirty
// rects. Assembled + emitted ONLY when the producer reports an actual change. Surfaces ADDS the identity
// (surface-id) that 003's per-producer FFrameReadySeam lacks — the relay, not a re-emit.
struct FSurfaceReadiness
{
	FSurfaceId Surface;
	FSurfaceReadyFrame Frame;
};

// The neutral Egress seam — Texture Egress backs this at integration (today a recording sink in tests).
// Mirrors 003's FFrameReadySeam idiom (a TFunction), one layer up and surface-addressed. Default unset until
// Egress wires it.
using FSurfaceReadinessSeam = TFunction<void(const FSurfaceReadiness&)>;

// --- The composed sibling seams (neutral; backed by 003/005 at integration) ---

// The surface's document (005). RenderFrame lays out the document at the surface's fixed available width and
// returns its paint ops as the neutral batch (005 ILayoutEngine::Relayout backs this; the batch is its
// FLayoutResult paint ops). Surfaces calls it, carries the batch to the target — never interprets it.
class ISurfaceDocument
{
public:
	virtual ~ISurfaceDocument() = default;
	virtual FSurfacePaintBatch RenderFrame(int32 AvailableWidth) = 0;
};

// The surface's render target (003's FSurfaceRasterizer) — composed at the PRODUCER granularity so the
// swap/dirty/frame-ready 003 built stay one layer up (no duplication). ProduceFrame rasterizes the batch into
// the back buffer + swaps IFF it changed, returning true on a real change (=> a frame-ready); GetReadyFrame
// exposes the published front buffer + dirty rects after such a frame. NO Resize: dims are fixed at creation.
// Surfaces holds this as a neutral handle, never touches a CPU bitmap, never imports CathodeRasterizer.
class ISurfaceRenderTarget
{
public:
	virtual ~ISurfaceRenderTarget() = default;

	virtual bool ProduceFrame(const FSurfacePaintBatch& Batch) = 0;
	virtual FSurfaceReadyFrame GetReadyFrame() const = 0;

	virtual int32 GetWidth() const = 0;
	virtual int32 GetHeight() const = 0;
	virtual ESurfacePixelFormat GetFormat() const = 0;
};

// --- Registration + the per-surface world-object ---

// A device declaring "I have a screen": explicit, FIXED dims/format + the neutral document and render-target
// handles (both injected by integration — 005 doc, 003 producer). Ownership transfers to the registry, which
// binds them into a surface; lifecycle frees them on unregister (no leak). The device-spawn caller is
// integration.
struct FSurfaceRegistration
{
	int32 Width = 0;
	int32 Height = 0;
	ESurfacePixelFormat Format = ESurfacePixelFormat::PremultipliedBGRA8;
	TUniquePtr<ISurfaceDocument> Document;
	TUniquePtr<ISurfaceRenderTarget> Target;
};

// The per-surface WORLD-OBJECT — identity + FIXED dims/format + the neutral document and render-target
// handles. RenderAndRelay is the bespoke per-surface frame pump: render the document -> produce into the
// target -> on a real change, assemble the surface-addressed readiness and emit it to Egress; returns true
// iff a frame was relayed. (Assembly is Slice 2; the relay is Slice 4 — declared here as the seam shape.)
class FSurface
{
public:
	FSurface(FSurfaceId InId, FSurfaceRegistration&& Registration);

	FSurfaceId GetId() const { return Id; }
	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }
	ESurfacePixelFormat GetFormat() const { return Format; }

	bool RenderAndRelay(const FSurfaceReadinessSeam& Egress);

private:
	FSurfaceId Id;
	int32 Width = 0;          // fixed at construction — immutable (no resize, by design)
	int32 Height = 0;         // fixed at construction — immutable
	ESurfacePixelFormat Format = ESurfacePixelFormat::PremultipliedBGRA8;
	TUniquePtr<ISurfaceDocument> Document;
	TUniquePtr<ISurfaceRenderTarget> Target;
};

// The runtime->surfaces registry — the many-surfaces model: one runtime, many surfaces, same global state.
// Register a declared screen (-> a surface id), unregister (destroy, no leak), look one up, count them, and
// DriveAll (the one runtime drives each live surface — render + relay; only changed surfaces emit). The
// registration/drive seam is generalizable: the device-spawn caller and the Egress consumer are integration.
class ISurfaceRegistry
{
public:
	virtual ~ISurfaceRegistry() = default;

	virtual FSurfaceId Register(FSurfaceRegistration&& Registration) = 0;
	virtual void Unregister(FSurfaceId Surface) = 0;
	virtual FSurface* Find(FSurfaceId Surface) = 0;
	virtual int32 Num() const = 0;

	virtual void DriveAll(const FSurfaceReadinessSeam& Egress) = 0;
};

// Construct the registry. The single place the many-surfaces bookkeeping is created (Core only; no vendor,
// no sibling import). (Provisional in Slice 1; the concrete registry + module land in Slices 2-3.)
CATHODESURFACES_API TUniquePtr<ISurfaceRegistry> CreateSurfaceRegistry();

// --- Knob inventory (initial; minimal, NO tuning surface — matches prior sprints) ---
//
//   knob                  | owner                | tunable? | default
//   ----------------------|----------------------|----------|------------------------------------------
//   buffer count          | inherited-003        | no       | 2 (FDoubleBuffer fixes double-buffering)
//   pixel format          | inherited-003        | no       | PremultipliedBGRA8 (DefaultSurfaceFormat)
//   dirty-rect granularity| inherited-003        | no       | 003 FDirtyRegion / EDirtyRegionGranularity
//   per-surface dims      | registration-INPUT   | n/a      | none — the device sizes it (native, no scale)
//   per-surface format    | registration-INPUT   | n/a      | PremultipliedBGRA8 (overridable per surface)
//   registry capacity     | our-bespoke          | no       | unbounded (no cap)
//
// None are runtime-tunable. Dims/format are set at registration and IMMUTABLE thereafter (no resize).

} // namespace Cathode
