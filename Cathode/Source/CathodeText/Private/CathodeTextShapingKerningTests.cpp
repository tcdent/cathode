#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeText.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// Real HarfBuzz hb_shape execution, proven DIRECTLY on CathodeText's API (a server LEAF — NO
// CathodeLayout dep, NO ITextMeasurer adapter, NO Layout wiring; the adapter + "measurement reaches Layout" are
// INTEGRATION, not this module). The TAUTOLOGY-FREE discriminator = the KERNING RELATION: measure("AV") < measure("A")+measure("V")
// — a kerning pair shaped as ONE run shrinks below the naive sum; ONLY real hb_shape applying the font's GPOS/kern yields
// the <; a naive/proportional/fixed path gives ==. PAIRED with a NON-KERNING control (measure("II") == sum EXACTLY) — the
// two-pair set bites a hardcode-smaller path. + the CORROBORATING trio (non-zero / longer->wider / larger-font->wider,
// secondary — a proportional path fakes them). HarfBuzz owns shaping correctness — NO hand-assert of its exact advances.
//
// WIN64 EXECUTED-NOT-SKIPPED: HarfBuzz is Win64-only (CathodeText.Build.cs). The shaping runs under #if PLATFORM_WINDOWS;
// a non-Win64 build FAILS (AddError) rather than vacuously passing with the assertions #if'd out — the green PROVES the
// Win64 path executed (the [WIN64-EXECUTED] marker + the real non-zero advances only the executed hb_shape path emits).
//
// FONT: LiberationSans-Regular.ttf (committed test fixture, Liberation Sans v2.1.5, SIL OFL 1.1; Tests/Fixtures/PROVENANCE.md), loaded
// via FFontRegistry -> FFileHelper -> hb_blob_create. Kern feature ON (the default FTextFeatures.bKerning=true).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTextShapingKerningTest,
	"Cathode.Text.Shaping.KerningRelation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTextShapingKerningTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

#if PLATFORM_WINDOWS
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	if (!Plugin.IsValid()) { AddError(TEXT("could not locate the Cathode plugin base dir (IPluginManager)")); return false; }
	const FString FontPath = Plugin->GetBaseDir() / TEXT("Source/CathodeText/Private/Tests/Fixtures/LiberationSans-Regular.ttf");
	if (!FPaths::FileExists(FontPath)) { AddError(FString::Printf(TEXT("the committed font fixture is not on disk: %s"), *FontPath)); return false; }

	// Register the real font -> bytes (FFileHelper) -> the shaper builds hb_blob/hb_face/hb_font. Run the FIRST real hb_shape.
	TUniquePtr<IFontRegistry> Registry = CreateFontRegistry();
	const FFontHandle Font = Registry->RegisterFontFile(FontPath);
	TUniquePtr<ITextShaper> Shaper = CreateHarfBuzzShaper(*Registry);

	// measure(text, size) = the real shaper's Width (sum of hb_shape's x-advances), kern ON (the default FTextFeatures).
	auto Measure = [&](const TCHAR* Text, float SizePx) -> float
	{
		FShapeRequest Req;
		Req.Text = FString(Text);
		Req.Font = Font;
		Req.FontSizePixels = SizePx;
		// Req.Features default: bKerning=true (kern ON — explicitly NOT disabled, so GPOS kerning applies).
		return Shaper->Shape(Req).Metrics.Width;
	};

	const float Size = 48.0f;
	const float wAV   = Measure(TEXT("AV"), Size);
	const float wA    = Measure(TEXT("A"), Size);
	const float wV    = Measure(TEXT("V"), Size);
	const float wII   = Measure(TEXT("II"), Size);
	const float wI    = Measure(TEXT("I"), Size);
	const float wAVAV = Measure(TEXT("AVAV"), Size);
	const float wAV96 = Measure(TEXT("AV"), 96.0f);

	// STEP-0 EMPIRICAL ADVANCES + the WIN64-EXECUTED marker (AddInfo logs regardless of pass/fail; only the executed
	// hb_shape path produces these real values). The boundary report reads the actual advances + the kern-delta from here.
	AddInfo(FString::Printf(TEXT("[WIN64-EXECUTED][021-KERN] real hb_shape advances @48px: AV=%.3f A=%.3f V=%.3f (A+V=%.3f, kern-delta=%.3f) | control II=%.3f I+I=%.3f | AVAV=%.3f | AV@96=%.3f"),
		wAV, wA, wV, wA + wV, (wA + wV) - wAV, wII, wI + wI, wAVAV, wAV96));

	// Prove the Win64 path EXECUTED: non-zero real advances (a #if'd-out / no-shape path cannot produce these).
	TestTrue(FString::Printf(TEXT("[WIN64-EXECUTED] hb_shape produced non-zero real advances (A=%.3f V=%.3f I=%.3f)"), wA, wV, wI),
		wA > 0.0f && wV > 0.0f && wI > 0.0f);

	// (1) THE KERNING RELATION (primary, tautology-free): measure("AV") < measure("A")+measure("V") — the font's GPOS/kern
	// applied by REAL hb_shape (a naive/proportional/fixed path gives ==). If >= : the font didn't kern 'AV' -> switch pair/font + flag.
	TestTrue(FString::Printf(TEXT("(kerning) measure('AV')=%.3f < measure('A')+measure('V')=%.3f (Liberation kerns 'AV')"), wAV, wA + wV),
		wAV < wA + wV);

	// (2) THE NON-KERNING CONTROL: measure("II") == measure("I")+measure("I") EXACTLY (Liberation does not kern 'II' — no GPOS
	// adjustment -> exact sum). Bites a hardcode-smaller path (which would shrink the control too).
	TestTrue(FString::Printf(TEXT("(non-kerning control) measure('II')=%.3f == measure('I')+measure('I')=%.3f EXACTLY (no kern -> exact sum; tol 0.01px)"), wII, wI + wI),
		FMath::IsNearlyEqual(wII, wI + wI, 0.01f));

	// (3) CORROBORATING TRIO (secondary — a proportional path fakes these; the kerning < is the real discriminator).
	TestTrue(FString::Printf(TEXT("(corroborating) non-zero: measure('AV')=%.3f > 0"), wAV), wAV > 0.0f);
	TestTrue(FString::Printf(TEXT("(corroborating) longer->wider: measure('AVAV')=%.3f > measure('AV')=%.3f"), wAVAV, wAV), wAVAV > wAV);
	TestTrue(FString::Printf(TEXT("(corroborating) larger-font->wider: measure('AV'@96)=%.3f > measure('AV'@48)=%.3f"), wAV96, wAV), wAV96 > wAV);

	return true;
#else
	// WIN64 EXECUTED-NOT-SKIPPED (the anti-vacuous-green guard): HarfBuzz is Win64-only; do NOT trivially pass on a non-Win64
	// build (the assertions would be #if'd out -> a meaningless green). Fail explicitly so a non-Win64 run can never report
	// Success for this proof. (This path is dormant on Win64; it guards a stray non-Win64 run.)
	AddError(TEXT("Cathode.Text.Shaping.KerningRelation requires Win64 (HarfBuzz is Win64-only); the real shaper did not execute on this platform — a non-Win64 pass would be vacuous."));
	return false;
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
