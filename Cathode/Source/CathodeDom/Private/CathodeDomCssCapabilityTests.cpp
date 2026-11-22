#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeCssCapability.h"
#include "CathodeCssAllowList.h" // the single-sourced enumeration (ParseLitehtmlPropertyNames, CssAuditedExclusions, CssCathodeComputed)
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// The CSS capability declaration's BEHAVIOR-PRESERVATION proof + the element.style ACCEPT-AND-IGNORE proof.
// IsDeclaredCssProperty reads the BAKED engine-derived set (CathodeCssAllowListGenerated, litehtml -7 +2). The
// litehtml-coherence of that baked set (committed==regen + the +2/-7 guards) and the exclusion member cross-check live
// in CathodeCssAllowListTests. Here:
//  - BehaviorPreservation: IsDeclaredCssProperty (baked-backed) accepts EXACTLY the litehtml -7 +2 set, property-by-
//    property (every 110 accepted; the 7 + a never-name rejected; the +2/-7 edges; CaseSensitive preserved).
//  - Validation: the element.style accept-and-ignore drives IsDeclaredCssProperty through the REAL JS path (the live proof).

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

// THE INVERTED ORACLE: present = string_id.h:163-295 parse-names MINUS the audited exclusions (calls CathodeCssAllowList's
// ParseLitehtmlPropertyNames + CssAuditedExclusions, the production codegen home). This is the 108-name Present oracle
// (NO +2), distinct from the codegen's full allow-list set (110 = 108 + 2).
void DeriveCssPresentSet(const FString& StringIdH, TSet<FString>& Out)
{
	ParseLitehtmlPropertyNames(StringIdH, Out);
	for (const FString& Ex : CssAuditedExclusions()) { Out.Remove(Ex); }
}

// The 2 non-litehtml props (opacity, letter-spacing) are CathodeCssAllowList's CssCathodeComputed (single-sourced).
} // namespace

// ===== BEHAVIOR PRESERVATION: IsDeclaredCssProperty reads the BAKED engine-derived set.
// Re-derive litehtml -7 +2 IN the test (build-time, string_id.h present -- NOT a runtime re-parse; PRODUCTION reads the
// baked accessor) and assert IsDeclaredCssProperty accepts EXACTLY that set PROPERTY-BY-PROPERTY (every 110 accepted; the
// 7 + a never-name rejected; the +2/-7 edges; CaseSensitive preserved). SET-equality, NOT count==110 -- a different 110
// would fail the per-name checks. =====
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeCssCapabilityBehaviorPreservationTest,
	"Cathode.Css.Capability.BehaviorPreservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeCssCapabilityBehaviorPreservationTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	const FString Dir = CathodePluginDir();
	if (Dir.IsEmpty()) { AddError(TEXT("could not resolve the Cathode plugin dir")); return false; }
	FString StringIdH;
	const FString StringIdPath = FPaths::Combine(Dir, TEXT("Source"), TEXT("ThirdParty"), TEXT("litehtml"), TEXT("include"), TEXT("litehtml"), TEXT("string_id.h"));
	if (!FFileHelper::LoadFileToString(StringIdH, *StringIdPath)) { AddError(FString::Printf(TEXT("could not read %s"), *StringIdPath)); return false; }

	// The reference set == litehtml -7 (DeriveCssPresentSet) + the 2 cathode_computed = litehtml -7 +2 (110). The SAME
	// projection the codegen bakes (CathodeCssAllowListGenerated).
	TSet<FString> Expected;
	DeriveCssPresentSet(StringIdH, Expected);                         // litehtml - 7 = 108
	for (const FString& C : CssCathodeComputed()) { Expected.Add(C); } // + 2 = 110
	TestEqual(TEXT("[025-S3] the re-derived reference is 110 (108 + 2) -- sanity, NOT the proof"), Expected.Num(), 110);

	// SET-EQUALITY, property-by-property (NOT count==110): every one of the 110 declared names is ACCEPTED.
	int32 Accepted = 0;
	for (const FString& Name : Expected)
	{
		if (IsDeclaredCssProperty(Name)) { ++Accepted; }
		else { AddError(FString::Printf(TEXT("[025-S3] declared '%s' is NOT accepted by IsDeclaredCssProperty (baked != old mirror)"), *Name)); }
	}
	TestEqual(TEXT("[025-S3] ALL 110 litehtml -7 +2 names ACCEPTED by IsDeclaredCssProperty (baked-backed, property-by-property)"), Accepted, 110);

	// The 7 audited exclusions are REJECTED (real litehtml names, but excluded -- the -7 edge, property-by-property).
	for (const FString& Ex : CssAuditedExclusions())
	{
		TestFalse(FString::Printf(TEXT("[025-S3] excluded '%s' is REJECTED by IsDeclaredCssProperty"), *Ex), IsDeclaredCssProperty(Ex));
	}

	// THE EXPLICIT EDGES -- the two a raw litehtml-name set would get wrong, plus member/non-member controls:
	TestTrue(TEXT("[025-S3] +2 edge: 'opacity' ACCEPTED (cathode_computed -- NOT a litehtml parse name)"), IsDeclaredCssProperty(TEXT("opacity")));
	TestTrue(TEXT("[025-S3] +2 edge: 'letter-spacing' ACCEPTED (cathode_computed)"), IsDeclaredCssProperty(TEXT("letter-spacing")));
	TestTrue(TEXT("[025-S3] member: 'color' ACCEPTED (a litehtml parse name)"), IsDeclaredCssProperty(TEXT("color")));
	TestTrue(TEXT("[025-S3] member: 'display' ACCEPTED (a litehtml parse name)"), IsDeclaredCssProperty(TEXT("display")));
	TestFalse(TEXT("[025-S3] -7 edge: 'font-variant' REJECTED (a real litehtml name, but audited-excluded)"), IsDeclaredCssProperty(TEXT("font-variant")));
	TestFalse(TEXT("[025-S3] non-member: 'grid-template-columns' REJECTED (never a litehtml parse name)"), IsDeclaredCssProperty(TEXT("grid-template-columns")));
	TestFalse(TEXT("[025-S3] non-member: 'foobar' REJECTED (not a property)"), IsDeclaredCssProperty(TEXT("foobar")));

	// CASE-SENSITIVITY (the lookup is ESearchCase::CaseSensitive; a TSet<FString> default would FLIP to
	// IgnoreCase): an UPPERCASED declared name is NOT accepted.
	TestFalse(TEXT("[025-S3] case-sensitive: 'COLOR' is NOT accepted (CaseSensitive preserved -- not flipped to IgnoreCase)"), IsDeclaredCssProperty(TEXT("COLOR")));

	// EVIDENCE (AddInfo prints on PASS — the per-point proof in the log; the assertions above are the test).
	AddInfo(FString::Printf(TEXT("[025-S3] behavior-preservation PROVEN: %d/%d declared names ACCEPTED property-by-property; all %d audited exclusions REJECTED; edges OK (opacity+letter-spacing ACCEPTED, color+display ACCEPTED, font-variant+grid-template-columns+foobar REJECTED, 'COLOR' REJECTED [CaseSensitive]) -- baked set == the retired mirror's accepted set"),
		Accepted, Expected.Num(), CssAuditedExclusions().Num()));
	return true;
}

// ===== element.style ACCEPT-AND-IGNORE: unsupported write does NOT reach the canonical 'style' attribute =====
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

	// CUSTOM properties (--x, the var() theming) stay ACCEPTED (not dropped by the declared-name filter).
	TestTrue(TEXT("custom property '--brand' is accepted (var() theming) — reaches the style attribute"),
		EvalBool("(function(){ __el.style.setProperty('--brand','#0af'); return (__el.getAttribute('style')||'').indexOf('--brand')>=0; })()"));

	// cssText FILTERS the unsupported (keeps declared + custom, drops OUT).
	TestTrue(TEXT("cssText filters unsupported: 'color:red;grid-area:x' keeps color, drops grid"),
		EvalBool("(function(){ __el.style.cssText='color:red;grid-area:foo'; var s=__el.getAttribute('style')||''; return s.indexOf('color')>=0 && s.indexOf('grid')<0; })()"));

	// SELF-GUARD — the BREAKING GEOMETRY: a cssText with MULTIPLE declared props INCLUDING font-size. The
	// cssText case above (color+grid-area, one declared/one unsupported) structurally can NOT see a declared prop dropped or
	// re-serialized.
	// (5a) all-declared multi-prop round-trips VERBATIM — color AND font-size both survive, exact serialization.
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
