#include "CathodeLayout.h"

#include "CathodeLayoutFontCache.h"
#include "CathodeObservationRead.h"
#include "CathodePaintRouting.h"
#include "CathodeRelayout.h"
#include "CathodeResolver.h"        // sprint 009: IResourceResolver
#include "CathodeResourceRouting.h" // sprint 009: the litehtml-free container->resolver routing helpers

#include "Containers/StringConv.h"

// litehtml — the layout/CSS engine. Every litehtml::/lt:: token is confined to this translation unit;
// swapping engines means reimplementing this file and the factory, nothing else. The include path is
// wired by the execution-approval sprint's build (the headers are vendored under
// vendor/lib/cathode/litehtml/include, built from source then); no build runs this sprint.
THIRD_PARTY_INCLUDES_START
// UE's check()/verify() assertion macros (from CoreMinimal) are function-like and collide with litehtml
// METHOD names (e.g. litehtml::media_query::check(...)) — the preprocessor expands the macro over the
// declaration and mangles it (C2062/C2334/C2208). THIRD_PARTY_INCLUDES_START only quiets warnings, not
// this. Push + undef the pair across the litehtml include, then restore them so our own code keeps check().
#pragma push_macro("check")
#pragma push_macro("verify")
#undef check
#undef verify
#include "litehtml.h"
#pragma pop_macro("verify")
#pragma pop_macro("check")
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

namespace
{

// litehtml -> neutral edge conversions (thin; litehtml types in, our types out).
FBox2f ToBox(const litehtml::position& Pos)
{
	return FBox2f(
		FVector2f(static_cast<float>(Pos.x), static_cast<float>(Pos.y)),
		FVector2f(static_cast<float>(Pos.x + Pos.width), static_cast<float>(Pos.y + Pos.height)));
}

FColor ToColor(const litehtml::web_color& C)
{
	return FColor(C.red, C.green, C.blue, C.alpha);
}

EBorderStyle ToBorderStyle(litehtml::border_style Style)
{
	switch (Style)
	{
	case litehtml::border_style_solid:  return EBorderStyle::Solid;
	case litehtml::border_style_dashed: return EBorderStyle::Dashed;
	case litehtml::border_style_dotted: return EBorderStyle::Dotted;
	case litehtml::border_style_double: return EBorderStyle::Double;
	default:                            return EBorderStyle::None;
	}
}

FBorderSide ToSide(const litehtml::border& Side)
{
	return FBorderSide{ static_cast<float>(Side.width), ToBorderStyle(Side.style), ToColor(Side.color) };
}

FCornerRadii ToRadii(const litehtml::border_radiuses& R)
{
	// TODO-PREBUILD(struct-field): verify litehtml::border_radiuses field names (top_left_x, top_right_x,
	// bottom_right_x, bottom_left_x) vs the vendored header.
	FCornerRadii Out;
	Out.TopLeft = static_cast<float>(R.top_left_x);
	Out.TopRight = static_cast<float>(R.top_right_x);
	Out.BottomRight = static_cast<float>(R.bottom_right_x);
	Out.BottomLeft = static_cast<float>(R.bottom_left_x);
	return Out;
}

// The litehtml document_container — the central callback seam litehtml calls during render. SKELETON
// (sprint 005 Slice 2): all callbacks exist and ROUTE — text -> the font cache + ITextMeasurer; paint ->
// the neutral paint-op sink; config -> inputs. The detailed param TRANSLATION (litehtml type -> neutral
// op params) and border decomposition are Slice 4; here they route with minimal/stubbed params. litehtml
// appears ONLY in this file.
class FCathodeDocumentContainer final : public litehtml::document_container
{
public:
	// TODO-PREBUILD(override): every document_container method below must EXACTLY match litehtml's
	// pure-virtual signatures (these are overrides, not new functions) — litehtml has changed several
	// across versions (create_font, draw_background, set_clip, get_image_size, create_element, ...).
	// Verify the full interface against the vendored litehtml; any mismatch is a hard compile error.
	FCathodeDocumentContainer(FLayoutFontCache& InFontCache, TArray<FLayoutPaintOp>& InSink,
		int32 InClientWidth, int32 InClientHeight, int32 InDpi, const FString& InDefaultFont, int32 InDefaultFontSize,
		IResourceResolver* InResolver) // sprint 009: the resource resolver (nullable; null == pre-hook stub behavior)
		: FontCache(InFontCache)
		, Sink(InSink)
		, ClientWidth(InClientWidth)
		, ClientHeight(InClientHeight)
		, Dpi(InDpi)
		, DefaultFontSize(InDefaultFontSize)
		, DefaultFontUtf8(*InDefaultFont) // stable UTF-8 copy for get_default_font_name()
		, Resolver(InResolver)
	{
	}

	// --- Text: route to the font cache + the neutral measurement seam ---

	// TODO-PREBUILD(signature): create_font has varied most across litehtml — the param list (decoration,
	// the font_metrics* out-param) and the uint_ptr return. Verify exactly vs the vendored header.
	litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic,
		unsigned int /*decoration*/, litehtml::font_metrics* fm) override
	{
		const bool bItalic = (italic == litehtml::font_style_italic);
		const uint64 Handle = FontCache.ResolveFont(UTF8_TO_TCHAR(faceName), size, weight, bItalic);
		// 013 slice 4 (B/b1): capture {face, weight} keyed by the handle. font-family/font-weight are member-only in
		// css_properties (no getter); css().get_font() -> this handle is the only public read path back to them.
		FontByHandle.Add(static_cast<litehtml::uint_ptr>(Handle), FCapturedFont{ FString(UTF8_TO_TCHAR(faceName)), weight });
		if (fm != nullptr)
		{
			float Ascent = 0.0f, Descent = 0.0f, Height = 0.0f;
			FontCache.GetFontMetrics(Handle, Ascent, Descent, Height);
			fm->ascent = FMath::RoundToInt(Ascent);
			fm->descent = FMath::RoundToInt(Descent);
			fm->height = FMath::RoundToInt(Height);
		}
		return static_cast<litehtml::uint_ptr>(Handle);
	}

	void delete_font(litehtml::uint_ptr hFont) override
	{
		FontCache.ReleaseFont(static_cast<uint64>(hFont));
	}

	// 013 slice 4 (B/b1): read back the captured face/weight for a font handle (css().get_font()). CathodeLayout-side;
	// returns FString/int32 (no litehtml type crosses the FResolvedStyle seam).
	bool LookupFont(litehtml::uint_ptr Handle, FString& OutFace, int32& OutWeight) const
	{
		if (const FCapturedFont* Found = FontByHandle.Find(Handle)) { OutFace = Found->Face; OutWeight = Found->Weight; return true; }
		return false;
	}

	int text_width(const char* text, litehtml::uint_ptr hFont) override
	{
		return FMath::RoundToInt(FontCache.MeasureWidth(static_cast<uint64>(hFont), UTF8_TO_TCHAR(text)));
	}

	void draw_text(litehtml::uint_ptr /*hdc*/, const char* text, litehtml::uint_ptr hFont,
		litehtml::web_color color, const litehtml::position& pos) override
	{
		// Un-shaped: the run + font ref + color + position; the integration sprint shapes it via 004.
		EmitDrawText(Sink, UTF8_TO_TCHAR(text), FontCache.GetRef(static_cast<uint64>(hFont)),
			ToColor(color), FVector2f(static_cast<float>(pos.x), static_cast<float>(pos.y)), ToBox(pos));
	}

	// --- Paint: full translation to the neutral paint-op stream ---

	// TODO-PREBUILD(signature): draw_background takes a std::vector<background_paint> in newer litehtml;
	// older versions passed a single background_paint. Verify vs the vendored header.
	void draw_background(litehtml::uint_ptr /*hdc*/, const std::vector<litehtml::background_paint>& bg) override
	{
		for (const litehtml::background_paint& Layer : bg)
		{
			const FBox2f Box = ToBox(Layer.border_box);
			if (!Layer.image.empty())
			{
				EmitDrawImage(Sink, Box, UTF8_TO_TCHAR(Layer.image.c_str())); // bytes resolved by the Resource Resolver later
			}
			else
			{
				EmitFillRect(Sink, Box, ToColor(Layer.color));
			}
		}
	}

	void draw_borders(litehtml::uint_ptr /*hdc*/, const litehtml::borders& Borders,
		const litehtml::position& draw_pos, bool /*root*/) override
	{
		// Convert at the edge, then decompose (solid -> 4 edge fills; styled/rounded -> StrokeBorder).
		FBorderStroke Stroke;
		Stroke.Box = ToBox(draw_pos);
		Stroke.Top = ToSide(Borders.top);
		Stroke.Right = ToSide(Borders.right);
		Stroke.Bottom = ToSide(Borders.bottom);
		Stroke.Left = ToSide(Borders.left);
		Stroke.Radii = ToRadii(Borders.radius);
		DecomposeBorder(Sink, Stroke);
	}

	void draw_list_marker(litehtml::uint_ptr /*hdc*/, const litehtml::list_marker& Marker) override
	{
		if (!Marker.image.empty())
		{
			EmitDrawImage(Sink, ToBox(Marker.pos), UTF8_TO_TCHAR(Marker.image.c_str()));
		}
		else
		{
			EmitFillRect(Sink, ToBox(Marker.pos), ToColor(Marker.color));
		}
	}

	void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override
	{
		// sprint 009: route to the resolver (litehtml::string -> FString at the edge); dimensions need a 003
		// decode (RQ1) -> 0x0 this sprint (RouteImageSize), now via the routing layer not a hardcoded stub.
		int32 Width = 0, Height = 0;
		RouteImageSize(Resolver, src ? FString(UTF8_TO_TCHAR(src)) : FString(),
			baseurl ? FString(UTF8_TO_TCHAR(baseurl)) : FString(), Width, Height);
		sz.width = Width;
		sz.height = Height;
	}

	void load_image(const char* src, const char* baseurl, bool /*redraw_on_ready*/) override
	{
		// sprint 009: resolve the image bytes via the resolver (available for the 003 draw at integration).
		TArray<uint8> Bytes;
		RouteImageBytes(Resolver, src ? FString(UTF8_TO_TCHAR(src)) : FString(),
			baseurl ? FString(UTF8_TO_TCHAR(baseurl)) : FString(), Bytes);
	}

	// --- Config / inputs ---

	int pt_to_px(int pt) const override { return FMath::RoundToInt(pt * Dpi / 72.0f); }
	int get_default_font_size() const override { return DefaultFontSize; }
	const char* get_default_font_name() const override { return DefaultFontUtf8.Get(); }

	void get_client_rect(litehtml::position& client) const override
	{
		client.x = 0;
		client.y = 0;
		client.width = ClientWidth;
		client.height = ClientHeight;
	}

	void get_media_features(litehtml::media_features& media) const override
	{
		media.type = litehtml::media_type_screen;
		media.width = ClientWidth;
		media.height = ClientHeight;
		media.device_width = ClientWidth;
		media.device_height = ClientHeight;
	}

	// --- Remaining callbacks: not used by the rendering surface (no-op / default) ---

	// TODO-PREBUILD(signature): set_clip changed across litehtml (older took extra bool valid_x/valid_y
	// params). Verify the (position, border_radiuses) form vs the vendored header.
	void set_clip(const litehtml::position& Pos, const litehtml::border_radiuses& /*r*/) override { EmitClipPush(Sink, ToBox(Pos)); }
	void del_clip() override { EmitClipPop(Sink); }
	void set_caption(const char* /*caption*/) override {}
	void set_base_url(const char* /*base_url*/) override {}
	void link(const std::shared_ptr<litehtml::document>& /*doc*/, const litehtml::element::ptr& /*el*/) override {}
	void on_anchor_click(const char* /*url*/, const litehtml::element::ptr& /*el*/) override {}
	void set_cursor(const char* /*cursor*/) override {}
	void transform_text(litehtml::string& /*text*/, litehtml::text_transform /*tt*/) override {}
	void import_css(litehtml::string& text, const litehtml::string& url, litehtml::string& baseurl) override
	{
		// sprint 009: resolve the linked stylesheet via the resolver; on a hit, hand the CSS text to litehtml.
		FString Css;
		if (RouteLinkedCss(Resolver, FString(UTF8_TO_TCHAR(url.c_str())), FString(UTF8_TO_TCHAR(baseurl.c_str())), Css))
		{
			text = TCHAR_TO_UTF8(*Css);
		}
	}
	void get_language(litehtml::string& /*language*/, litehtml::string& /*culture*/) const override {}

	litehtml::element::ptr create_element(const char* /*tag_name*/, const litehtml::string_map& /*attributes*/,
		const std::shared_ptr<litehtml::document>& /*doc*/) override
	{
		return nullptr; // let litehtml create the default element
	}

private:
	FLayoutFontCache& FontCache;
	TArray<FLayoutPaintOp>& Sink;
	int32 ClientWidth;
	int32 ClientHeight;
	int32 Dpi;
	int32 DefaultFontSize;
	FTCHARToUTF8 DefaultFontUtf8; // stable UTF-8 default font name (init from the ctor's InDefaultFont)
	IResourceResolver* Resolver;  // sprint 009: nullable; the image/CSS resource callbacks route to it

	// 013 slice 4 (B/b1): handle -> {face, weight} captured at create_font, read back for font-family/font-weight.
	struct FCapturedFont { FString Face; int32 Weight = 400; };
	TMap<litehtml::uint_ptr, FCapturedFont> FontByHandle;
};

// 013 slice 4 — getComputedStyle value formatters (CSSOM resolved-value forms per Fork-A). litehtml-confined
// (web_color/css_length/enum -> FString); the FString crosses the FResolvedStyle seam engine-free.
static FString FmtCssPx(int Px) { return FString::Printf(TEXT("%dpx"), Px); }

static FString FmtCssColor(const litehtml::web_color& C)
{
	// CSSOM comma-form, never hex; opaque -> rgb(), else rgba() with a 0..1 alpha (transparent -> rgba(0,0,0,0)).
	if (C.alpha >= 255) { return FString::Printf(TEXT("rgb(%d,%d,%d)"), C.red, C.green, C.blue); }
	return FString::Printf(TEXT("rgba(%d,%d,%d,%g)"), C.red, C.green, C.blue, static_cast<double>(C.alpha) / 255.0);
}

static FString FmtCssLength(const litehtml::css_length& L)
{
	if (L.is_predefined()) { return TEXT("auto"); }
	switch (L.units())
	{
	case litehtml::css_units_px:         return FString::Printf(TEXT("%gpx"), L.val());
	case litehtml::css_units_percentage: return FString::Printf(TEXT("%g%%"), L.val()); // ON-RECORD: %/em return the computed form, not used-px (the used edges are inaccessible)
	default:                             return FString::Printf(TEXT("%gpx"), L.val());
	}
}

// margin/padding/border-width are single contract props -> uniform sides collapse to one value (margin:7px -> '7px').
static FString FmtCssSides(const litehtml::css_length& T, const litehtml::css_length& R, const litehtml::css_length& B, const litehtml::css_length& L)
{
	const FString Ts = FmtCssLength(T), Rs = FmtCssLength(R), Bs = FmtCssLength(B), Ls = FmtCssLength(L);
	if (Ts == Rs && Rs == Bs && Bs == Ls) { return Ts; }
	return FString::Printf(TEXT("%s %s %s %s"), *Ts, *Rs, *Bs, *Ls);
}

static FString FmtCssEnum(int Value, const char* Strings)
{
	return FString(UTF8_TO_TCHAR(litehtml::index_value(Value, Strings).c_str()));
}

// 014 slice 1 (CathodeComputed) — litehtml does NOT parse letter-spacing, so getComputedStyle sources it Cathode-side:
// parse the element's OWN inline 'style' attr ON-DEMAND (own-element-only; inheritance + rendering deferred to the
// litehtml build-how). A LENGTH value -> normalized px: 'letter-spacing:2px' -> '2px'; '0.1em' of font-size 16 -> '1.6px'
// (specified '0.1em' != computed '1.6px' -> the parse is load-bearing, not an echo). unset/absent/'normal'/rem/other-unit
// -> EMPTY (the documented own-inline partial; NOT 'normal'). FontSizePx is litehtml's resolved px (the em base). Engine-
// free (plain args, no litehtml type); the FString crosses the FResolvedStyle seam, so CathodeDom never sees litehtml.
static FString ParseOwnInlineLetterSpacing(const char* StyleAttr, int32 FontSizePx)
{
	if (StyleAttr == nullptr) { return FString(); }
	TArray<FString> Decls;
	FString(UTF8_TO_TCHAR(StyleAttr)).ParseIntoArray(Decls, TEXT(";"), true);
	for (const FString& Decl : Decls)
	{
		int32 Colon;
		if (!Decl.FindChar(TEXT(':'), Colon)) { continue; }
		if (!Decl.Left(Colon).TrimStartAndEnd().Equals(TEXT("letter-spacing"), ESearchCase::IgnoreCase)) { continue; }
		const FString Value = Decl.RightChop(Colon + 1).TrimStartAndEnd().ToLower();
		if (Value.EndsWith(TEXT("em")) && !Value.EndsWith(TEXT("rem"))) { return FString::Printf(TEXT("%gpx"), FCString::Atof(*Value.LeftChop(2)) * FontSizePx); }
		if (Value.EndsWith(TEXT("px")))                                 { return FString::Printf(TEXT("%gpx"), FCString::Atof(*Value.LeftChop(2))); }
		return FString(); // 'normal'/rem/other unit -> empty (the documented partial covers px/em lengths)
	}
	return FString(); // no own letter-spacing declaration -> empty (own-element-only: the unset / deferred-inheritance boundary)
}

// 014 slice 2 (CathodeComputed) — opacity is non-litehtml; getComputedStyle sources it Cathode-side: parse the element's
// OWN inline value ON-DEMAND. CSS computed-value rule = clamp(<number> | <percentage>, [0,1]) serialized as a plain number
// ('0.5'/'1'/'0'; 1.5->'1' & -0.2->'0' clamp; .5->'0.5' normalize; 90%->'0.9'). opacity does NOT inherit + initial = 1, so
// UNSET -> '1' (the correct CSS computed value; no cascade needed). A cascaded/stylesheet opacity is OWN-INLINE-ONLY here
// (litehtml discards opacity; we read only El's own attr) — deferred with the rasterizer build-how. Engine-free; FString
// crosses the FResolvedStyle seam, so CathodeDom never sees litehtml.
static FString ParseOwnInlineOpacity(const char* StyleAttr)
{
	if (StyleAttr != nullptr)
	{
		TArray<FString> Decls;
		FString(UTF8_TO_TCHAR(StyleAttr)).ParseIntoArray(Decls, TEXT(";"), true);
		for (const FString& Decl : Decls)
		{
			int32 Colon;
			if (!Decl.FindChar(TEXT(':'), Colon)) { continue; }
			if (!Decl.Left(Colon).TrimStartAndEnd().Equals(TEXT("opacity"), ESearchCase::IgnoreCase)) { continue; }
			const FString Value = Decl.RightChop(Colon + 1).TrimStartAndEnd();
			const bool bPercent = Value.EndsWith(TEXT("%"));
			const FString NumStr = (bPercent ? Value.LeftChop(1) : Value).TrimStartAndEnd();
			// a malformed declaration is invalid -> ignored -> the CSS initial '1' (opacity does NOT inherit).
			if (NumStr.IsEmpty() || !(FChar::IsDigit(NumStr[0]) || NumStr[0] == TEXT('.') || NumStr[0] == TEXT('-') || NumStr[0] == TEXT('+'))) { return FString(TEXT("1")); }
			float Number = FCString::Atof(*NumStr);
			if (bPercent) { Number /= 100.0f; }
			return FString::Printf(TEXT("%g"), FMath::Clamp(Number, 0.0f, 1.0f));
		}
	}
	return FString(TEXT("1")); // no own opacity declaration -> the CSS initial value (opacity does NOT inherit)
}

// The litehtml-backed relayout backend — the vendor edge of the IRelayoutBackend seam. Re-feeds markup
// through the document_container + renders + draws (which fires the paint callbacks into the sink). The
// complete path re-feeds the whole document; the intelligent path re-feeds only the affected subtree.
// litehtml is confined here; the controller that DECIDES which path is litehtml-free + tested separately.
class FLiteHtmlRelayoutBackend final : public IRelayoutBackend
{
public:
	FLiteHtmlRelayoutBackend(const ILayoutDocumentSource& InSource, FLayoutFontCache& InFontCache,
		TArray<FLayoutPaintOp>& InSink, IResourceResolver* InResolver) // sprint 009: pass the resolver to the container
		: Source(InSource)
		, Sink(InSink)
		, Container(InFontCache, InSink, DefaultViewportWidth, DefaultViewportHeight, DefaultDpi,
			TEXT("sans-serif"), DefaultFontSize, InResolver)
	{
	}

	virtual void RelayoutDocument(int32 AvailableWidth) override
	{
		Render(Source.GetDocumentHtml(), Source.GetDocumentCss(), AvailableWidth);
	}

	virtual void RelayoutSubtree(int32 SubtreeRoot, int32 ContainingWidth) override
	{
		// Re-feed the subtree in isolation (its paint ops come out relative to the subtree), then SPLICE:
		// translate the newly-added ops into document coordinates by the boundary's offset. OffsetPaintOps
		// is OUR built+tested transform; the offset VALUE is the vendor edge (B's prior-layout position).
		const int32 Before = Sink.Num();
		Render(Source.GetSubtreeHtml(SubtreeRoot), Source.GetDocumentCss(), ContainingWidth);
		const FVector2f BoundaryOffset = GetBoundaryOffset(SubtreeRoot);
		OffsetPaintOps(TArrayView<FLayoutPaintOp>(Sink.GetData() + Before, Sink.Num() - Before), BoundaryOffset);
	}

	// Post-render geometry read (T1): resolve our NodeId to litehtml's placed element (via the data-node-id map
	// built at render) and return litehtml's OWN computed box. ForceInit (zero) for an unknown/unrendered NodeId.
	FBox2f QueryBox(int32 NodeId) const
	{
		if (const litehtml::element::ptr* Element = NodeElementMap.Find(NodeId))
		{
			return ToBox((*Element)->get_placement());
		}
		return FBox2f(ForceInit);
	}

	// 013 slice 4 — the per-property computed-value read (the gate-flip's vendor edge): litehtml's OWN resolved value
	// for NodeId's element, in the CSSOM Fork-A form. Empty for an unmapped node, opacity (build-how-pending), or the
	// HELD font-family/font-weight (member-only/no-getter; browse-design B-ruling pending). FString crosses engine-free.
	FString QueryComputedValue(int32 NodeId, const FString& Property) const
	{
		const litehtml::element::ptr* ElementPtr = NodeElementMap.Find(NodeId);
		if (ElementPtr == nullptr || !*ElementPtr) { return FString(); }
		const litehtml::element::ptr& El = *ElementPtr;
		const litehtml::css_properties& Css = El->css();
		const litehtml::position Box = El->get_placement();

		// BOX-MODEL used px from get_placement (NOT css()); top/left -> 'auto' on a static element.
		if (Property == TEXT("width"))  { return FmtCssPx(Box.width); }
		if (Property == TEXT("height")) { return FmtCssPx(Box.height); }
		if (Property == TEXT("left"))   { return Css.get_position() == litehtml::element_position_static ? FString(TEXT("auto")) : FmtCssPx(Box.left()); }
		if (Property == TEXT("top"))    { return Css.get_position() == litehtml::element_position_static ? FString(TEXT("auto")) : FmtCssPx(Box.top()); }

		// ENUMS -> litehtml's COMPUTED keyword (the typed getter via index_value over the _strings table).
		if (Property == TEXT("display"))    { return FmtCssEnum(Css.get_display(),    style_display_strings); }
		if (Property == TEXT("position"))   { return FmtCssEnum(Css.get_position(),   element_position_strings); }
		if (Property == TEXT("text-align")) { return FmtCssEnum(Css.get_text_align(), text_align_strings); }
		if (Property == TEXT("visibility")) { return FmtCssEnum(Css.get_visibility(), visibility_strings); }

		// COLOR -> rgb()/rgba() comma-form.
		if (Property == TEXT("color"))            { return FmtCssColor(Css.get_color()); }
		if (Property == TEXT("background-color")) { return FmtCssColor(Css.get_bg().m_color); }

		// FONT (readable getters) -> resolved px.
		if (Property == TEXT("font-size"))   { return FmtCssPx(Css.get_font_size()); }
		if (Property == TEXT("line-height")) { return FmtCssPx(Css.get_line_height()); }

		// BOX-MODEL EDGES — option-(i) css()-COMPUTED (browse-design Fork-A refinement). LIMITATION (on-record):
		// %/layout-resolved margin/padding return the COMPUTED form, NOT used-px (the used edges are on litehtml's
		// PRIVATE render_item, inaccessible via the public API). Correct used px only for ABSOLUTE/default lengths.
		// Future = expose render_item edges -> 1-line-real to used-px. Do NOT reconstruct used-from-computed.
		if (Property == TEXT("margin"))       { const litehtml::css_margins& M = Css.get_margins(); return FmtCssSides(M.top, M.right, M.bottom, M.left); }
		if (Property == TEXT("padding"))      { const litehtml::css_margins& P = Css.get_padding(); return FmtCssSides(P.top, P.right, P.bottom, P.left); }
		if (Property == TEXT("border-width")) { const litehtml::css_borders& B = Css.get_borders(); return FmtCssSides(B.top.width, B.right.width, B.bottom.width, B.left.width); }

		// FONT-FAMILY / FONT-WEIGHT — option-(b1): member-only/no-getter in css_properties, read back via the font HANDLE
		// (css().get_font()) -> the Container's capture map. FIDELITY NOTE (on-record): font-family returns the resolved
		// FACE ('Arial'), which may differ from the CSSOM computed-LIST ('Arial, sans-serif') -- the computed-list member
		// has no public getter; resolved-face proves resolution (fine for slice-4). font-weight is the full-fidelity
		// resolved NUMERIC ('400'/'700'), no enum mapping.
		if (Property == TEXT("font-family") || Property == TEXT("font-weight"))
		{
			FString Face; int32 Weight = 400;
			if (Container.LookupFont(Css.get_font(), Face, Weight))
			{
				return Property == TEXT("font-family") ? Face : FString::Printf(TEXT("%d"), Weight);
			}
			return FString(); // font not yet resolved (e.g. an element litehtml created no font for) -> empty
		}

		// letter-spacing (014 slice 1, CathodeComputed) -> litehtml does NOT parse it; source the element's OWN inline
		// value Cathode-side (NAME-keyed branch, not status-keyed), em resolved against litehtml's computed font-size.
		// own-element-only: inheritance is deferred to the litehtml build-how (no Cathode-side parent walk).
		if (Property == TEXT("letter-spacing"))
		{
			return ParseOwnInlineLetterSpacing(El->get_attr("style"), Css.get_font_size());
		}

		// opacity (014 slice 2, CathodeComputed) -> non-litehtml; parse the element's OWN inline value Cathode-side
		// (NAME-keyed), clamp [0,1], unset -> the CSS initial '1' (opacity does NOT inherit). Cascaded + rendering deferred.
		if (Property == TEXT("opacity"))
		{
			return ParseOwnInlineOpacity(El->get_attr("style"));
		}

		return FString(); // an unmapped node / non-contract property -> empty (every contract property has a branch above)
	}

private:
	static constexpr int32 DefaultViewportWidth = 1920;
	static constexpr int32 DefaultViewportHeight = 1080;
	static constexpr int32 DefaultDpi = 96;
	static constexpr int32 DefaultFontSize = 16;

	void Render(const FString& Html, const FString& Css, int32 Width)
	{
		// TODO-PREBUILD(signature): verify litehtml::document::createFromString overload — arg types
		// (const char* vs std::string), the master/user stylesheet params (litehtml::master_css), and that
		// older litehtml took a litehtml::context instead. High variance across litehtml versions.
		litehtml::document::ptr Doc = litehtml::document::createFromString(
			TCHAR_TO_UTF8(*Html), &Container, litehtml::master_css, TCHAR_TO_UTF8(*Css));
		if (Doc)
		{
			Doc->render(Width);
			Doc->draw(static_cast<litehtml::uint_ptr>(0), 0, 0, nullptr); // fires the container paint callbacks -> sink
			// RETAIN the laid-out document + (re)build the NodeId<->element map (T1): get_placement() is valid
			// after render(), so post-render geometry queries (QueryBox) resolve our NodeId to litehtml's placed
			// element via the data-node-id attribute the DOM serializer emits.
			RenderedDoc = Doc;
			RebuildNodeElementMap();
		}
	}

	FVector2f GetBoundaryOffset(int32 /*SubtreeRoot*/) const
	{
		// TODO(validation pass): query the boundary element's position in the prior full layout from
		// litehtml (the vendor edge). The OffsetPaintOps transform that consumes this is built + tested.
		return FVector2f::ZeroVector;
	}

	// Walk the laid-out litehtml tree, mapping each element carrying a data-node-id attribute (the DOM
	// serializer's correlation handle, preserved through litehtml's parse) to its placed element::ptr.
	void RebuildNodeElementMap()
	{
		NodeElementMap.Reset();
		if (RenderedDoc)
		{
			IndexElement(RenderedDoc->root());
		}
	}

	void IndexElement(const litehtml::element::ptr& Element)
	{
		if (!Element)
		{
			return;
		}
		if (const char* IdAttr = Element->get_attr("data-node-id"))
		{
			NodeElementMap.Add(FCStringAnsi::Atoi(IdAttr), Element);
		}
		for (const litehtml::element::ptr& Child : Element->children())
		{
			IndexElement(Child);
		}
	}

	const ILayoutDocumentSource& Source;
	TArray<FLayoutPaintOp>& Sink;
	FCathodeDocumentContainer Container;
	litehtml::document::ptr RenderedDoc;                // retained laid-out document (T1 geometry queries)
	TMap<int32, litehtml::element::ptr> NodeElementMap; // NodeId -> placed litehtml element (via data-node-id)
};

// The litehtml-backed layout engine. Owns the font cache + the paint-op sink + the relayout controller
// (litehtml-free: dirty tracking + affected-subtree selection + the both-path drive) + the litehtml
// backend. SetDocument wires the source + creates the backend; MarkNodeDirty feeds the controller;
// Relayout drives the controller (which selects complete vs intelligent and drives the backend) and
// returns the routed paint ops. The geometry/style read-out is Slice 5.
class FLiteHtmlLayoutEngine final : public ILayoutEngine
{
public:
	explicit FLiteHtmlLayoutEngine(ITextMeasurer& InMeasurer)
		: FontCache(InMeasurer)
	{
	}

	void SetDocument(const ILayoutDocumentSource& InSource) override
	{
		Source = &InSource;
		Controller.SetDocument(InSource);
		Backend = MakeUnique<FLiteHtmlRelayoutBackend>(InSource, FontCache, Sink, Resolver);
	}

	void MarkNodeDirty(int32 NodeId, EDirtyKind Kind) override
	{
		Controller.MarkNodeDirty(NodeId, Kind);
	}

	void SetResourceResolver(IResourceResolver* InResolver) override
	{
		// sprint 009: store the resolver; SetDocument passes it to the backend/container. Call before SetDocument.
		Resolver = InResolver;
	}

	FLayoutResult Relayout(int32 AvailableWidth) override
	{
		Sink.Reset();
		if (Backend.IsValid())
		{
			Controller.Relayout(AvailableWidth, *Backend);
		}

		FLayoutResult Result;
		Result.PaintOps = Sink; // the paint ops the container routed during the re-feed render

		// Observation read-out: walk the laid-out tree -> per-node geometry (box + bounded resolved style).
		// The walk + bounded mapping are ours (CathodeObservationRead); the box + raw computed values come
		// through the vendor-edge queries below (litehtml's positioned tree / cascade).
		if (Source != nullptr)
		{
			WalkGeometry(*Source,
				[this](int32 NodeId) { return QueryNodeBox(NodeId); },
				[this](int32 NodeId) { return QueryNodeStyle(NodeId); },
				Result.Geometry);
		}
		return Result;
	}

private:
	FLayoutFontCache FontCache;
	TArray<FLayoutPaintOp> Sink;
	FRelayoutController Controller;
	TUniquePtr<FLiteHtmlRelayoutBackend> Backend;
	const ILayoutDocumentSource* Source = nullptr;
	IResourceResolver* Resolver = nullptr; // sprint 009: passed to the backend/container at SetDocument

	// --- Vendor edges: map our NodeId onto litehtml's laid-out element + read its box / computed value.
	//     Stubbed this sprint (no render runs); the validation pass binds them to the rendered document.

	FBox2f QueryNodeBox(int32 NodeId) const
	{
		// T1: read litehtml's OWN computed box for NodeId via the backend's data-node-id map (resolved from the
		// retained, laid-out document). The walk/assembly around this (WalkGeometry) is tested separately.
		return Backend.IsValid() ? Backend->QueryBox(NodeId) : FBox2f(ForceInit);
	}

	FResolvedStyle QueryNodeStyle(int32 NodeId) const
	{
		// The bounded mapping is OURS (queries only the contract property set); the per-property value is the
		// vendor edge (litehtml's computed cascade) read by the backend (013 slice 4, gate flipped to real values).
		return MapResolvedStyle([this, NodeId](const FString& Property) { return QueryComputedValue(NodeId, Property); });
	}

	FString QueryComputedValue(int32 NodeId, const FString& Property) const
	{
		// 013 slice 4: delegate to the backend's litehtml-confined per-property read (the empty stub is gone).
		return Backend.IsValid() ? Backend->QueryComputedValue(NodeId, Property) : FString();
	}
};

} // namespace

TUniquePtr<ILayoutEngine> CreateLiteHtmlLayoutEngine(ITextMeasurer& Measurer)
{
	return MakeUnique<FLiteHtmlLayoutEngine>(Measurer);
}

} // namespace Cathode
