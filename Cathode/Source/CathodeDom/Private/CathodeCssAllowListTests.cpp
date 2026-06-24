#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CathodeCssAllowList.h"           // the codegen + the enumeration (off litehtml)
#include "CathodeCssAllowListGenerated.h"  // GetGeneratedCssAllowListNames (the baked runtime set)
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"

// 025 S2 — the CSS allow-list codegen driver + guards (Model A, OFF litehtml — same shape as the S1 .d.ts). The
// regenerate writes BOTH css-allow-list.toml (build/linter) AND CathodeCssAllowListGenerated.cpp (the baked runtime
// array); the coherence asserts committed==regen for BOTH; the +2/-7 guards are the ENGINE SUBTRACTION (bound to
// ParseLitehtmlPropertyNames — non-circular), since the coherence alone does NOT catch a wrong exclusion.
static TAutoConsoleVariable<int32> CVarRegenCssAllowList(TEXT("cathode.RegenCssAllowList"), 0,
	TEXT("025 S2: set 1 + run Cathode.Authoring.CssAllowList.Regenerate to write css-allow-list.toml + the runtime .cpp from litehtml."));

namespace
{
	using namespace Cathode;

	FString PluginBaseDir()
	{
		const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
		return P.IsValid() ? P->GetBaseDir() : FString();
	}
	FString CssAllowListTomlPath() { const FString D = PluginBaseDir(); return D.IsEmpty() ? FString() : (D / TEXT("Authoring/css-allow-list.toml")); }
	FString CssAllowListCppPath()  { const FString D = PluginBaseDir(); return D.IsEmpty() ? FString() : (D / TEXT("Source/CathodeDom/Private/CathodeCssAllowListGenerated.cpp")); }
}

// --- THE DRIVER (regenerate-mode; writes BOTH artifacts only when cathode.RegenCssAllowList=1) ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCathodeCssAllowListRegenerateTest, "Cathode.Authoring.CssAllowList.Regenerate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCathodeCssAllowListRegenerateTest::RunTest(const FString& Parameters)
{
	if (CVarRegenCssAllowList.GetValueOnGameThread() != 1)
	{
		AddInfo(TEXT("[025-S2] regenerate-mode OFF (cathode.RegenCssAllowList != 1) — not writing the committed artifacts (normal-suite no-op)."));
		return true;
	}
	const FString Toml = GenerateCssAllowListToml();
	const FString Cpp = GenerateCssAllowListCpp();
	if (Toml.IsEmpty() || Cpp.IsEmpty()) { AddError(TEXT("[025-S2] codegen returned empty — could not load string_id.h?")); return false; }
	const FString TomlPath = CssAllowListTomlPath();
	const FString CppPath = CssAllowListCppPath();
	if (TomlPath.IsEmpty() || CppPath.IsEmpty()) { AddError(TEXT("[025-S2] could not resolve the artifact paths")); return false; }
	if (!FFileHelper::SaveStringToFile(Toml, *TomlPath)) { AddError(FString::Printf(TEXT("[025-S2] failed to write %s"), *TomlPath)); return false; }
	if (!FFileHelper::SaveStringToFile(Cpp, *CppPath))   { AddError(FString::Printf(TEXT("[025-S2] failed to write %s"), *CppPath));  return false; }
	AddInfo(FString::Printf(TEXT("[025-S2] REGENERATED css-allow-list.toml (%d chars) + CathodeCssAllowListGenerated.cpp (%d chars)"), Toml.Len(), Cpp.Len()));
	return true;
}

// --- THE COHERENCE (committed == regenerated, for BOTH .toml and the runtime .cpp; off litehtml) ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCathodeCssAllowListCoherenceTest, "Cathode.Authoring.CssAllowList.Coherence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCathodeCssAllowListCoherenceTest::RunTest(const FString& Parameters)
{
	auto CheckOne = [this](const FString& Path, const FString& Regen, const TCHAR* Label)
	{
		FString Committed;
		if (!FFileHelper::LoadFileToString(Committed, *Path)) { AddError(FString::Printf(TEXT("[025-S2] could not read committed %s"), *Path)); return; }
		Committed.ReplaceInline(TEXT("\r\n"), TEXT("\n")); // git autocrlf on the box -> compare content, not EOL
		const bool bMatch = Committed.Equals(Regen, ESearchCase::CaseSensitive);
		AddInfo(FString::Printf(TEXT("[025-S2] coherence %s: committed=%d regen=%d match=%d"), Label, Committed.Len(), Regen.Len(), bMatch ? 1 : 0));
		TestTrue(FString::Printf(TEXT("[025-S2] the committed %s == the regeneration off litehtml (a string_id.h add/remove flows through; a stale commit REDs)"), Label), bMatch);
	};
	CheckOne(CssAllowListTomlPath(), GenerateCssAllowListToml(), TEXT("css-allow-list.toml"));
	CheckOne(CssAllowListCppPath(),  GenerateCssAllowListCpp(),  TEXT("CathodeCssAllowListGenerated.cpp"));
	return true;
}

// --- THE +2 / -7 GUARDS (the ENGINE SUBTRACTION, bound to ParseLitehtmlPropertyNames — non-circular) ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCathodeCssAllowListGuardsTest, "Cathode.Authoring.CssAllowList.Guards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCathodeCssAllowListGuardsTest::RunTest(const FString& Parameters)
{
	FString StringIdH;
	if (!LoadLitehtmlStringIdH(StringIdH)) { AddError(TEXT("[025-S2] could not load string_id.h for the guards")); return false; }
	TSet<FString> Litehtml; ParseLitehtmlPropertyNames(StringIdH, Litehtml);
	TArray<FString> SetArr; BuildCssAllowListSet(StringIdH, SetArr);
	const TSet<FString> Set(SetArr);

	// +2 GUARD (non-circular): (generated MINUS litehtml-parse-names) == exactly {opacity, letter-spacing} — a 3rd
	// silent non-litehtml prop makes the diff 3 -> RED. NOT "the set contains opacity+letter-spacing" (that's the
	// circular bug — the output asserting its own contents). The subtraction is bound to ParseLitehtmlPropertyNames.
	const TSet<FString> GenNonLitehtml = Set.Difference(Litehtml);
	TSet<FString> Expected2; for (const FString& C : CssCathodeComputed()) { Expected2.Add(C); }
	AddInfo(FString::Printf(TEXT("[025-S2] +2 guard: generated-minus-litehtml=%d (expected 2)"), GenNonLitehtml.Num()));
	TestTrue(TEXT("[025-S2] +2 guard: (generated set MINUS litehtml parse-names) == exactly {opacity, letter-spacing} (a 3rd silent non-litehtml prop flags)"),
		GenNonLitehtml.Difference(Expected2).Num() == 0 && Expected2.Difference(GenNonLitehtml).Num() == 0);

	// -7 GUARD (a): every audited exclusion is a REAL litehtml parse-name (a bogus exclusion of a non-litehtml name flags).
	for (const FString& Ex : CssAuditedExclusions())
	{
		TestTrue(FString::Printf(TEXT("[025-S2] -7 guard (a): exclusion '%s' is a real litehtml parse-name (subset of ParseLitehtmlPropertyNames)"), *Ex),
			Litehtml.Contains(Ex));
	}
	// -7 GUARD (b): the generated set's LITEHTML entries == litehtml parse-names MINUS the 7 — an 8th SILENT exclusion
	// (dropping a real litehtml/contract prop) makes the diff non-zero -> RED. The coherence alone can't catch this (a
	// silently-added 8th, once regen+committed, passes committed==regen), so the subtraction has its OWN guard.
	const TSet<FString> GenLitehtml = Set.Intersect(Litehtml);
	TSet<FString> LitehtmlMinus7 = Litehtml;
	for (const FString& Ex : CssAuditedExclusions()) { LitehtmlMinus7.Remove(Ex); }
	AddInfo(FString::Printf(TEXT("[025-S2] -7 guard: generated-litehtml-entries=%d litehtml-minus-7=%d"), GenLitehtml.Num(), LitehtmlMinus7.Num()));
	TestTrue(TEXT("[025-S2] -7 guard (b): the generated set's litehtml entries == ParseLitehtmlPropertyNames MINUS the 7 (an 8th silent exclusion flags)"),
		GenLitehtml.Difference(LitehtmlMinus7).Num() == 0 && LitehtmlMinus7.Difference(GenLitehtml).Num() == 0);
	return true;
}

// --- THE BAKED RUNTIME SET == the projection (the committed .cpp's array == litehtml -7 +2; S3 reads this baked set) ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCathodeCssAllowListRuntimeSetTest, "Cathode.Authoring.CssAllowList.RuntimeSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCathodeCssAllowListRuntimeSetTest::RunTest(const FString& Parameters)
{
	FString StringIdH;
	if (!LoadLitehtmlStringIdH(StringIdH)) { AddError(TEXT("[025-S2] could not load string_id.h for the runtime-set check")); return false; }
	TArray<FString> Projected; BuildCssAllowListSet(StringIdH, Projected);
	const TSet<FString> P(Projected);
	const TSet<FString> B(GetGeneratedCssAllowListNames());
	AddInfo(FString::Printf(TEXT("[025-S2] baked runtime set=%d projected=%d"), B.Num(), P.Num()));
	TestTrue(TEXT("[025-S2] the baked runtime set (CathodeCssAllowListGenerated.cpp) == the projection (litehtml -7 +2) — S3's runtime source matches the codegen"),
		P.Difference(B).Num() == 0 && B.Difference(P).Num() == 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
