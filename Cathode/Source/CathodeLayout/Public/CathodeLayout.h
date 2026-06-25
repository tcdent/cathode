#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

// The Cathode layout swap boundary: our internal contract over the layout/CSS engine (today litehtml).
// This header is the SWAP BOUNDARY — litehtml's own types (litehtml::, lt::) appear ONLY in the impl,
// never here, so the engine can be replaced without touching the bespoke orchestration (the
// document_container routing, dirty tracking + affected-subtree selection, the re-feed relayout, border
// decomposition, the geometry/style read).
//
// Altitude is the handful of ops we need — set the document, mark a node dirty, relayout (-> positioned
// geometry + a neutral paint-op stream) — NOT a 1:1 re-export of the litehtml API and NOT a re-creation
// of a layout engine. litehtml's computed boxes/cascade are vendor value (never hand-asserted, like
// Blend2D pixels / HarfBuzz positions); we test only our orchestration on top.
//
// Layout is Core + litehtml ONLY — NO Blend2D/BL*, NO HarfBuzz/hb_*, NO CathodeRasterizer/CathodeText
// coupling. The paint-op output is OUR neutral type (mapped to 003 FPaintOp at integration); text
// measurement is a neutral ITextMeasurer (backed by 004 at integration); the DOM is a neutral source.

namespace Cathode
{

// Engine-neutral font reference (the font litehtml's create_font resolved, via our ITextMeasurer). No
// hb_*/BL*; the integration sprint maps it to 004's FFontHandle.
struct FLayoutFontRef
{
	int32 Id = INDEX_NONE;
	bool IsValid() const { return Id != INDEX_NONE; }
	bool operator==(const FLayoutFontRef& Other) const { return Id == Other.Id; }
};

// How a node changed — decides paint-only vs relayout (the affected-subtree intelligence). PaintOnly
// (color/background/visual) SKIPS layout entirely; Layout (text/size) and Structure (child add/remove)
// trigger relayout of the affected subtree.
enum class EDirtyKind : uint8
{
	PaintOnly,
	Layout,
	Structure,
};

// --- The neutral paint-op output (OUR primitives; mapped to 003 FPaintOp at integration; no BL*) ---

enum class EBorderStyle : uint8
{
	None,
	Solid,
	Dashed,
	Dotted,
	Double,
};

struct FBorderSide
{
	float Width = 0.0f;
	EBorderStyle Style = EBorderStyle::None;
	FColor Color = FColor::Black;
};

struct FCornerRadii
{
	float TopLeft = 0.0f;
	float TopRight = 0.0f;
	float BottomRight = 0.0f;
	float BottomLeft = 0.0f;
};

// A styled/rounded border (any styled side OR any corner radius) — OUR neutral stroke primitive. Layout
// PRODUCES this now; the Rasterizer-side draw op that CONSUMES it is the 003 COVERAGE.md #3 stroke op
// (deferred sibling). Solid square borders decompose to 4 FillBox edges instead.
struct FBorderStroke
{
	FBox2f Box = FBox2f(ForceInit);
	FCornerRadii Radii;
	FBorderSide Top;
	FBorderSide Right;
	FBorderSide Bottom;
	FBorderSide Left;
};

enum class ELayoutPaintOpKind : uint8
{
	FillRect,
	FillBox,      // box / solid-border edge fill
	DrawImage,
	DrawText,     // un-shaped run; the integration sprint shapes it via 004 -> a 003 DrawGlyphRun
	StrokeBorder, // styled/rounded border (the neutral stroke primitive)
	ClipPush,     // push a clip rect (maps to the 003 Rasterizer ClipToRect at integration)
	ClipPop,      // pop the clip (maps to the 003 ResetClip)
	ClearRect,
};

// One paint operation — Layout's neutral output, emitted by the document_container routing. A fat tagged
// struct (like the 003 FPaintOp) the integration sprint maps to 003 FPaintOp. Geometry in our/Core types
// (FBox2f/FVector2f/FColor); no litehtml/BL* leak.
struct FLayoutPaintOp
{
	ELayoutPaintOpKind Kind = ELayoutPaintOpKind::FillRect;

	FBox2f Rect = FBox2f(ForceInit); // fill rect/box, image dest, clear, text bounds
	FColor Color = FColor::Black;    // fill / text color

	FString ImageSource;             // DrawImage — the url() ref (kept for trace/debug; the bytes below are the source)
	TArray<uint8> ImageBytes;        // 029 #4 — DrawImage RESOLVED bytes (the layout resolves ONCE with the baseurl + carries; the composition decodes+draws — no re-resolve). Owned, deep-copied by value through the op sink.

	FString Text;                    // DrawText — un-shaped run (integration shapes via 004)
	FLayoutFontRef Font;             // DrawText — font ref
	FVector2f Position = FVector2f::ZeroVector; // DrawText — origin

	FBorderStroke Border;            // StrokeBorder — styled/rounded border geometry
};

// --- The observation read (backs getBoundingClientRect / getComputedStyle; NOT the author contract) ---

// A node's resolved computed style, BOUNDED to the contract's CSS properties (not the full CSSOM):
// property name -> resolved value (opaque vendor string). The bounded property SET is ours; the values
// are litehtml's cascade.
struct FResolvedStyle
{
	TMap<FString, FString> Properties;
};

// Per-node observation record: the element -> box geometry plus its bounded resolved style.
struct FLayoutNodeGeometry
{
	int32 NodeId = INDEX_NONE;
	FBox2f Box = FBox2f(ForceInit);
	FResolvedStyle Style;
};

// The laid-out result: the neutral paint-op stream + the per-node geometry read (box + bounded resolved
// style, backing the observer's getBoundingClientRect / getComputedStyle) + the document size.
struct FLayoutResult
{
	TArray<FLayoutPaintOp> PaintOps;
	TArray<FLayoutNodeGeometry> Geometry;
	int32 DocumentWidth = 0;
	int32 DocumentHeight = 0;
};

// --- The neutral seams ---

// The retained-DOM source (the document to lay out) — a generalizable, evolvable seam the future DOM
// component implements. It exposes the tree STRUCTURE (for affected-subtree selection) and the MARKUP
// (for the re-feed relayout: full document, or a subtree for the intelligent path). The dirty-DOM-fork
// contingency may add a mutable-tree accessor later without breaking this seam.
class ILayoutDocumentSource
{
public:
	virtual ~ILayoutDocumentSource() = default;

	// Tree structure — what the affected-subtree selection walks.
	virtual int32 GetRootNode() const = 0;
	virtual int32 GetParentNode(int32 NodeId) const = 0;
	virtual void GetChildNodes(int32 NodeId, TArray<int32>& OutChildren) const = 0;
	// True when the node's box is fixed independent of its descendants (explicit dims / replaced / root)
	// — the relayout boundary the selection walks up to.
	virtual bool IsLayoutBoundary(int32 NodeId) const = 0;

	// Markup — what the re-feed relayout mechanism consumes.
	virtual FString GetDocumentHtml() const = 0;       // full re-feed (complete-rerender path)
	virtual FString GetSubtreeHtml(int32 NodeId) const = 0; // subtree re-feed (intelligent path)
	virtual FString GetDocumentCss() const = 0;
};

// Text measurement input — the neutral seam Layout's document_container calls (litehtml create_font /
// text_width). Backed by 004's shaper at integration; Layout never reaches into HarfBuzz.
class ITextMeasurer
{
public:
	virtual ~ITextMeasurer() = default;

	virtual FLayoutFontRef ResolveFont(const FString& Face, int32 PixelSize, int32 Weight, bool bItalic) = 0;
	virtual float MeasureWidth(const FString& Text, FLayoutFontRef Font) = 0;
	virtual void GetFontMetrics(FLayoutFontRef Font, float& OutAscent, float& OutDescent, float& OutHeight) = 0;
};

// Sprint 009: the Resource Resolver the document_container's image/CSS/font callbacks route to. Defined by
// CathodeResolver; referenced here by pointer only (forward-declared — the engine impl includes its header).
class IResourceResolver;

// The layout engine — the swap boundary. SetDocument (the retained-DOM source), MarkNodeDirty (drives
// the affected-subtree selection), Relayout (re-feed baseline behind the seam: complete when nothing
// localizes, the affected subtree when dirty marks do) -> positioned geometry + the neutral paint ops.
class ILayoutEngine
{
public:
	virtual ~ILayoutEngine() = default;

	virtual void SetDocument(const ILayoutDocumentSource& Source) = 0;
	virtual void MarkNodeDirty(int32 NodeId, EDirtyKind Kind) = 0;
	virtual FLayoutResult Relayout(int32 AvailableWidth) = 0;

	// Sprint 009: inject the Resource Resolver the document_container's load_image/get_image_size/import_css
	// route to. Non-pure default no-op (a null/unset resolver == the pre-hook stub behavior; keeps existing
	// mock implementers byte-identical). Call before SetDocument. The litehtml-backed engine overrides it.
	virtual void SetResourceResolver(IResourceResolver* Resolver) {}
};

// Construct the litehtml-backed layout engine over a text measurer. The single place litehtml is bound;
// swapping the layout engine swaps only this implementation.
CATHODELAYOUT_API TUniquePtr<ILayoutEngine> CreateLiteHtmlLayoutEngine(ITextMeasurer& Measurer);

} // namespace Cathode
