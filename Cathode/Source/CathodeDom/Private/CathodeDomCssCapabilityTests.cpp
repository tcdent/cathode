#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeCssCapability.h"
#include "CathodeCssAllowList.h" // 025 S2 — the moved enumeration (ParseLitehtmlPropertyNames, CssAuditedExclusions, CssCathodeComputed); single-sourced here, no longer duplicated below
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// 013 slice 1 — the CSS capability declaration's COHERENCE GUARD + the element.style ACCEPT-AND-IGNORE proof.
// COHERENCE: the hand-authored C++ mirror's Present partition == litehtml's string_id.h parse-names MINUS the 7 audited
// exclusions (read as FILES so the test catches vendor drift). The guard BITES if the mirror is perturbed off the engine.
// 025 S2 UPDATE: the capability-manifest.toml is RETIRED (Model B), so the former mirror==manifest lockstep is removed;
// the mirror is now proven against litehtml (the engine) directly. The mirror itself lives till S3 (where the runtime
// consumer re-points to the baked CathodeCssAllowListGenerated set; the codegen's own coherence is in CathodeCssAllowListTests).

namespace
{
using namespace Cathode;

class FStubLayoutEngine final : public ILayoutEngine
{
public:
	virtual void SetDocument(const ILayoutDocumentSource&) override {}
	virtual void MarkNodeDirty(int32, EDirtyKind) override {}
	virtual FLayoutResult Relayout(int32) override { return FLayoutResult(); }
};

JSValue Eval(JSContext* Context, const char* Source)
{
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-csscap-test>", JS_EVAL_TYPE_GLOBAL);
}

FString CathodePluginDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	return Plugin.IsValid() ? Plugin->GetBaseDir() : FString();
}

// ParseManifestCssNames + ParseLitehtmlPropertyNames retired/moved at 025 S2: the manifest is retired (Model B), and
// ParseLitehtmlPropertyNames now lives in CathodeCssAllowList (the production codegen home, included above) — single-sourced.

// Extract css_properties.h computed-member identifiers (m_<name>). SETTLED ROLE (browse-design ratified the inverted
// method): NOT the inclusion filter — a member-filter silently drops shorthands whose decomposition lives in litehtml's
// non-vendored parse() .cpp (flex-flow was hole #7; #8 unknowable from headers). The member set is the CROSS-CHECK that
// justifies the 3 parse-but-not-computed exclusions: assert font-variant/counter-reset/counter-increment stay member-less,
// so a future litehtml member-add for any flags its exclusion for reclassification.
void ParseCssPropertyMembers(const FString& Header, TSet<FString>& Out)
{
	TArray<FString> Lines;
	Header.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		// a bare member DECLARATION "<type> m_<name>;" — the token is immediately followed by ';' (skip the
		// constructor-initializer uses "m_<name>(...)" where it is followed by '(').
		int32 Idx = Line.Find(TEXT("m_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
		while (Idx != INDEX_NONE)
		{
			int32 End = Idx + 2;
			while (End < Line.Len() && (FChar::IsAlnum(Line[End]) || Line[End] == TEXT('_'))) { ++End; }
			if (End < Line.Len() && Line[End] == TEXT(';') && End > Idx + 2)
			{
				Out.Add(Line.Mid(Idx + 2, End - (Idx + 2)));
			}
			Idx = Line.Find(TEXT("m_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, End);
		}
	}
}

// CssAuditedExclusions moved to CathodeCssAllowList at 025 S2 (the production codegen home, included above) — single-sourced.

// THE INVERTED ORACLE: present = string_id.h:163-295 parse-names MINUS the audited exclusions (calls the moved
// ParseLitehtmlPropertyNames + CssAuditedExclusions). This is the 108-name Present oracle (NO +2) — kept test-local
// because it checks the mirror's Present PARTITION (108), distinct from the codegen's full allow-list set (110 = 108 + 2).
void DeriveCssPresentSet(const FString& StringIdH, TSet<FString>& Out)
{
	ParseLitehtmlPropertyNames(StringIdH, Out);
	for (const FString& Ex : CssAuditedExclusions()) { Out.Remove(Ex); }
}

// The 2 forward-declared non-litehtml props (opacity, letter-spacing) moved to CathodeCssAllowList at 025 S2 as
// CssCathodeComputed (the production codegen home, included above) — single-sourced; the test references it below.
} // namespace

// ===== COMPLETENESS GUARD (inverted method): mirror Present == string_id.h:163-295 parse-names MINUS the 7 audited
// exclusions (file-derived; bites on a litehtml name add/remove OR a mirror perturbation, BOTH directions); mirror ==
// manifest [css] lockstep; member CROSS-CHECK that the 3 parse-but-not-computed exclusions stay member-less. =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeCssCapabilityCoherenceTest,
	"Cathode.Css.Capability.Coherence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeCssCapabilityCoherenceTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	const FString Dir = CathodePluginDir();
	if (Dir.IsEmpty()) { AddError(TEXT("could not resolve the Cathode plugin dir")); return false; }

	FString StringIdH;
	const FString StringIdPath = FPaths::Combine(Dir, TEXT("Source"), TEXT("ThirdParty"), TEXT("litehtml"), TEXT("include"), TEXT("litehtml"), TEXT("string_id.h"));
	if (!FFileHelper::LoadFileToString(StringIdH, *StringIdPath)) { AddError(FString::Printf(TEXT("could not read %s"), *StringIdPath)); return false; }

	// 025 S2: the manifest is RETIRED (Model B); the mirror==manifest lockstep is removed (UPDATE-not-revert). The
	// mirror==litehtml-derived coherence (the ENGINE source of truth) is KEPT below — the mirror lives till S3 (where it's
	// replaced by the baked set + the css-allow-list regen-vs-committed coherence in CathodeCssAllowListTests).
	TSet<FString> LitehtmlNames;
	ParseLitehtmlPropertyNames(StringIdH, LitehtmlNames);
	TestTrue(TEXT("the litehtml string_id property-name parse found a real set (sanity)"), LitehtmlNames.Num() > 20);

	FString CssPropsH;
	const FString CssPropsPath = FPaths::Combine(Dir, TEXT("Source"), TEXT("ThirdParty"), TEXT("litehtml"), TEXT("include"), TEXT("litehtml"), TEXT("css_properties.h"));
	if (!FFileHelper::LoadFileToString(CssPropsH, *CssPropsPath)) { AddError(FString::Printf(TEXT("could not read %s"), *CssPropsPath)); return false; }
	TSet<FString> ComputedMembers;
	ParseCssPropertyMembers(CssPropsH, ComputedMembers);
	TestTrue(TEXT("the css_properties.h member parse found a real computed-surface set (sanity)"), ComputedMembers.Num() > 40);

	// THE 7 AUDITED EXCLUSIONS are single-sourced in CssAuditedExclusions(). GROUND each: it MUST be a real string_id.h
	// parse name, else the exclusion is a phantom (a name we exclude that litehtml never had). The 3 parse-but-not-computed
	// are additionally cross-checked member-less below; the 4 internals are member-backed url/accessor names excluded by name.
	for (const FString& Ex : CssAuditedExclusions())
	{
		TestTrue(FString::Printf(TEXT("exclusion '%s' is a real string_id.h parse name (grounded, not phantom)"), *Ex), LitehtmlNames.Contains(Ex));
	}
	static const TCHAR* const ParseButNotComputed[] = { TEXT("font-variant"), TEXT("counter-reset"), TEXT("counter-increment") };

	// DERIVE Present from the FILE (single-sourced inverted oracle): universe = string_id.h:163-295 parse-names MINUS the
	// audited exclusions. It bites on a litehtml name ADD (new name -> in DerivedPresent, absent from mirror -> fail) or REMOVE.
	TSet<FString> DerivedPresent;
	DeriveCssPresentSet(StringIdH, DerivedPresent);

	// Partition the mirror into Present / CathodeComputed — the only two statuses (BuildHowPending retired at 014 slice 2).
	// CathodeComputed = the forward-declared non-litehtml props whose value Cathode owns (own-inline at getComputedStyle read).
	TSet<FString> MirrorPresent, MirrorCathodeComputed;
	for (const FCssCapabilityEntry& E : GetCssCapabilityMirror())
	{
		(E.Status == ECssPropertyStatus::Present ? MirrorPresent : MirrorCathodeComputed).Add(E.Name);
	}

	// (1) mirror == manifest [css] lockstep RETIRED at 025 S2 (the manifest is gone — Model B). The mirror's coherence is
	// now proven against the ENGINE (litehtml string_id.h) directly via (2) below — mirror Present == the file-derived 108.

	// (2) COMPLETENESS, BOTH directions: mirror Present == DerivedPresent (string_id.h:163-295 minus exclusions).
	TestEqual(TEXT("mirror Present has no name OUTSIDE the derived set (no over-declaration)"), MirrorPresent.Difference(DerivedPresent).Num(), 0);
	TestEqual(TEXT("mirror Present is MISSING no derived name (bites on a litehtml name ADD)"), DerivedPresent.Difference(MirrorPresent).Num(), 0);

	// (3) MEMBER CROSS-CHECK: the 3 parse-but-not-computed exclusions stay member-LESS (a future litehtml member-add flags them).
	for (const TCHAR* Pnc : ParseButNotComputed)
	{
		const FString MemberForm = FString(Pnc).Replace(TEXT("-"), TEXT("_"));
		TestFalse(FString::Printf(TEXT("parse-but-not-computed '%s' has NO css_properties member (cross-check; a member-add would flag reclassification)"), Pnc), ComputedMembers.Contains(MemberForm));
	}

	// (4) the CathodeComputed (non-litehtml) props MUST be ABSENT from string_id.h (Axis-3 IN, not a litehtml parse name).
	for (const FString& Nl : MirrorCathodeComputed)
	{
		TestFalse(FString::Printf(TEXT("CathodeComputed '%s' is correctly ABSENT from string_id.h (that's WHY Cathode owns its value)"), *Nl), LitehtmlNames.Contains(Nl));
	}

	// (5) CLOSED-SET HYGIENE on the forward-declared 2 non-litehtml (reviewer standing gate; same discipline as the 7 exclusions).
	const TSet<FString> NonLitehtmlAudited(CssCathodeComputed());
	// (5a) the CathodeComputed mirror entries are EXACTLY the closed audited list — not an open bucket (both directions).
	TestEqual(TEXT("CathodeComputed mirror entries == the closed audited 2 (no over)"), MirrorCathodeComputed.Difference(NonLitehtmlAudited).Num(), 0);
	TestEqual(TEXT("CathodeComputed mirror entries == the closed audited 2 (no under)"), NonLitehtmlAudited.Difference(MirrorCathodeComputed).Num(), 0);
	// (5b) EXACT 110 = 108 + 2, DISJOINT: the file-derivation yields exactly 108 INDEPENDENT of the 2; no name in both partitions.
	TestEqual(TEXT("derived Present is exactly 108 (file-derivation, independent of the 2 CathodeComputed)"), DerivedPresent.Num(), 108);
	TestEqual(TEXT("mirror Present is exactly 108"), MirrorPresent.Num(), 108);
	TestEqual(TEXT("mirror CathodeComputed is exactly 2"), MirrorCathodeComputed.Num(), 2);
	TestEqual(TEXT("Present and CathodeComputed are DISJOINT (no overlap in 110 = 108 + 2)"), MirrorPresent.Intersect(MirrorCathodeComputed).Num(), 0);
	// (5c) none of the 2 leaks into the 108 file-derivation (each forward-declared name is OUTSIDE the derived set).
	for (const FString& Nl : CssCathodeComputed())
	{
		TestFalse(FString::Printf(TEXT("forward-declared '%s' does NOT leak into the 108 file-derivation"), *Nl), DerivedPresent.Contains(Nl));
	}

	// (6) THE CathodeComputed BUCKET is EXACTLY {opacity, letter-spacing} (the single non-Present status after build_how_pending
	// retired at slice 2). The status is INFORMATIONAL metadata here (the gate keys on NAME, not status; the READ-behavior
	// distinction is proven in the Cathode.Dom.Style.{LetterSpacing,Opacity} getComputedStyle tests). This asserts the bucket.
	TestTrue(TEXT("CathodeComputed bucket is EXACTLY {opacity, letter-spacing}"),
		MirrorCathodeComputed.Num() == 2 && MirrorCathodeComputed.Contains(TEXT("opacity")) && MirrorCathodeComputed.Contains(TEXT("letter-spacing")));

	// (7) DISCRIMINATING declared-vs-cascaded: a non-litehtml prop is DECLARED in the mirror yet is NOT a litehtml
	// computed-parse name -> not cascade-real (opacity stays empty; letter-spacing is Cathode-computed own-inline).
	// Contrast a Present prop (color): DECLARED AND a litehtml parse name -> cascade-real. Proves declared != cascaded.
	auto MirrorStatus = [](const FString& Name) -> int32 {
		for (const FCssCapabilityEntry& E : GetCssCapabilityMirror()) { if (Name.Equals(E.Name, ESearchCase::CaseSensitive)) { return (int32)E.Status; } }
		return -1;
	};
	TestEqual(TEXT("'opacity' is DECLARED CathodeComputed in the mirror (014 slice 2)"), MirrorStatus(TEXT("opacity")), (int32)ECssPropertyStatus::CathodeComputed);
	TestEqual(TEXT("'letter-spacing' is DECLARED CathodeComputed in the mirror (014 slice 1)"), MirrorStatus(TEXT("letter-spacing")), (int32)ECssPropertyStatus::CathodeComputed);
	TestFalse(TEXT("'opacity' is NOT a litehtml computed-parse name (Cathode owns the value, own-inline)"), LitehtmlNames.Contains(TEXT("opacity")));
	TestFalse(TEXT("'letter-spacing' is NOT a litehtml computed-parse name (that's WHY it's Cathode-computed own-inline)"), LitehtmlNames.Contains(TEXT("letter-spacing")));
	TestEqual(TEXT("contrast: 'color' is PRESENT in the mirror (declared)"), MirrorStatus(TEXT("color")), (int32)ECssPropertyStatus::Present);
	TestTrue(TEXT("contrast: 'color' IS a litehtml computed-parse name (declared AND cascade-real)"), LitehtmlNames.Contains(TEXT("color")));

	return true;
}

// ===== COMPLETENESS BITES BOTH DIRECTIONS — the discriminating boundary proof. A guard that cannot fail proves nothing:
// construct perturbed Present sets and assert the SAME symmetric-difference comparison the Coherence guard uses DETECTS
// an injected fake (over-declaration) and a dropped real name (under-declaration), with a clean control on the true set. =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeCssCapabilityCompletenessBitesTest,
	"Cathode.Css.Capability.CompletenessBites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeCssCapabilityCompletenessBitesTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	const FString Dir = CathodePluginDir();
	if (Dir.IsEmpty()) { AddError(TEXT("could not resolve the Cathode plugin dir")); return false; }
	FString StringIdH;
	const FString StringIdPath = FPaths::Combine(Dir, TEXT("Source"), TEXT("ThirdParty"), TEXT("litehtml"), TEXT("include"), TEXT("litehtml"), TEXT("string_id.h"));
	if (!FFileHelper::LoadFileToString(StringIdH, *StringIdPath)) { AddError(FString::Printf(TEXT("could not read %s"), *StringIdPath)); return false; }

	TSet<FString> DerivedPresent;
	DeriveCssPresentSet(StringIdH, DerivedPresent);
	TSet<FString> MirrorPresent;
	for (const FCssCapabilityEntry& E : GetCssCapabilityMirror()) { if (E.Status == ECssPropertyStatus::Present) { MirrorPresent.Add(E.Name); } }

	// INDEPENDENCE (reviewer gate: the 108 file-derivation bites independently of the 2): none of the forward-declared
	// non-litehtml props leaks into the file-derivation or the Present partition, so the perturbation below tests the 108 alone.
	for (const FString& Nl : CssCathodeComputed())
	{
		TestFalse(FString::Printf(TEXT("non-litehtml '%s' is OUTSIDE the file-derived 108 (bite domain independent of the 2)"), *Nl), DerivedPresent.Contains(Nl));
		TestFalse(FString::Printf(TEXT("non-litehtml '%s' is OUTSIDE the mirror Present partition"), *Nl), MirrorPresent.Contains(Nl));
	}

	// CONTROL: the real mirror Present is complete — zero diff BOTH directions (no false-positive; the guard is green on truth).
	TestEqual(TEXT("CONTROL: real mirror Present has zero over-declaration vs derived"), MirrorPresent.Difference(DerivedPresent).Num(), 0);
	TestEqual(TEXT("CONTROL: real mirror Present has zero under-declaration vs derived"), DerivedPresent.Difference(MirrorPresent).Num(), 0);

	// OVER-DECLARATION bite: inject a fake name the derivation can't contain -> the (Mirror - Derived) check DETECTS it.
	{
		TSet<FString> Over = MirrorPresent;
		Over.Add(TEXT("__cathode_perturb_fake__"));
		TestTrue(TEXT("BITE (over): an injected fake name IS caught by the over-declaration check (Mirror - Derived > 0)"), Over.Difference(DerivedPresent).Num() > 0);
	}
	// UNDER-DECLARATION bite: drop a real present name -> the (Derived - Mirror) check DETECTS the gap.
	{
		const bool bHadDisplay = MirrorPresent.Contains(TEXT("display"));
		TestTrue(TEXT("precondition: 'display' is a real present name available to drop"), bHadDisplay);
		TSet<FString> Under = MirrorPresent;
		Under.Remove(TEXT("display"));
		TestTrue(TEXT("BITE (under): a dropped real name IS caught by the under-declaration check (Derived - Mirror > 0)"), DerivedPresent.Difference(Under).Num() > 0);
	}

	return true;
}

// ===== element.style ACCEPT-AND-IGNORE (fork-3): unsupported write does NOT reach the canonical 'style' attribute =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeCssCapabilityValidationTest,
	"Cathode.Css.Capability.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeCssCapabilityValidationTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	FJsValue ElW = Doc->CreateElement(TEXT("div"));
	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__el", JS_DupValue(Context, ElW.GetValue()));
	JS_FreeValue(Context, Global);

	auto EvalBool = [&](const char* Expr) -> bool
	{
		JSValue R = Eval(Context, Expr);
		if (JS_IsException(R)) { JS_FreeValue(Context, R); AddError(FString::Printf(TEXT("JS threw: %s"), ANSI_TO_TCHAR(Expr))); return false; }
		bool bValue = false; const bool bOk = UnmarshalBool(Context, R, bValue);
		JS_FreeValue(Context, R);
		return bOk && bValue;
	};

	// DECLARED (Present) name accepted -> reaches the canonical 'style' attribute.
	TestTrue(TEXT("declared 'color' write reaches the style attribute"),
		EvalBool("(function(){ __el.style.color='red'; return __el.getAttribute('style').indexOf('color')>=0 && __el.style.color==='red'; })()"));

	// UNSUPPORTED (OUT) name SILENTLY DROPPED -> demonstrably ABSENT from the style attribute (NOT merely 'no error').
	TestTrue(TEXT("unsupported 'grid-template-columns' write is DROPPED — absent from the style attribute (gate flip)"),
		EvalBool("(function(){ __el.style.gridTemplateColumns='1fr 1fr'; var s=__el.getAttribute('style')||''; return s.indexOf('grid')<0 && __el.style.gridTemplateColumns===undefined; })()"));
	TestTrue(TEXT("setProperty('grid-gap',...) (unsupported) is DROPPED — absent from the style attribute"),
		EvalBool("(function(){ __el.style.setProperty('grid-gap','10px'); return (__el.getAttribute('style')||'').indexOf('grid')<0; })()"));

	// CUSTOM properties (--x, the var() theming) stay ACCEPTED (not regressed by the gate).
	TestTrue(TEXT("custom property '--brand' is accepted (var() theming) — reaches the style attribute"),
		EvalBool("(function(){ __el.style.setProperty('--brand','#0af'); return (__el.getAttribute('style')||'').indexOf('--brand')>=0; })()"));

	// cssText FILTERS the unsupported (keeps declared + custom, drops OUT).
	TestTrue(TEXT("cssText filters unsupported: 'color:red;grid-area:x' keeps color, drops grid"),
		EvalBool("(function(){ __el.style.cssText='color:red;grid-area:foo'; var s=__el.getAttribute('style')||''; return s.indexOf('color')>=0 && s.indexOf('grid')<0; })()"));

	// SELF-GUARD — the BREAKING GEOMETRY (review): a cssText with MULTIPLE declared props INCLUDING font-size. The prior
	// cssText case (color+grid-area, one declared/one unsupported) structurally could NOT see a declared prop dropped or
	// re-serialized; the multi-declared-incl-font-size cell lived only in 012's ElementStyle. Now it is IN the slice's own proof.
	// (5a) all-declared multi-prop round-trips VERBATIM — color AND font-size both survive, exact serialization (the cell that broke).
	TestTrue(TEXT("cssText multi-declared incl font-size round-trips VERBATIM (color AND font-size survive, exact bytes)"),
		EvalBool("(function(){ __el.style.cssText='color:red;font-size:12px'; return __el.getAttribute('style')==='color:red;font-size:12px' && __el.style.color==='red' && __el.style.fontSize==='12px'; })()"));
	// (5b) mixed: declared (color+font-size) + custom (--brand) KEPT, unsupported (grid-area) FILTERED — declared admitted, drop preserved.
	TestTrue(TEXT("cssText mixed: declared color+font-size + custom --brand KEPT, unsupported grid-area FILTERED"),
		EvalBool("(function(){ __el.style.cssText='color:red;font-size:12px;grid-area:foo;--brand:#0af'; var s=__el.getAttribute('style')||''; return s.indexOf('color')>=0 && s.indexOf('font-size')>=0 && s.indexOf('--brand')>=0 && s.indexOf('grid')<0 && __el.style.fontSize==='12px'; })()"));

	// NON-LITEHTML DECLARED (both CathodeComputed): both reach the style attribute (accepted by NAME) — acceptance into the
	// attr is the PRECONDITION for the on-demand own-inline parse at getComputedStyle read (value-side proofs: Cathode.Dom.Style.{Opacity,LetterSpacing}).
	TestTrue(TEXT("CathodeComputed 'opacity' is DECLARED — accepted, reaches the style attribute"),
		EvalBool("(function(){ __el.style.opacity='0.5'; return (__el.getAttribute('style')||'').indexOf('opacity')>=0; })()"));
	TestTrue(TEXT("CathodeComputed 'letter-spacing' is DECLARED — accepted, reaches the style attribute"),
		EvalBool("(function(){ __el.style.letterSpacing='2px'; return (__el.getAttribute('style')||'').indexOf('letter-spacing')>=0; })()"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
