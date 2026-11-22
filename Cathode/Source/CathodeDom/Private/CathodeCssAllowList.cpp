#include "CathodeCssAllowList.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

namespace Cathode
{

bool LoadLitehtmlStringIdH(FString& OutContents)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	if (!Plugin.IsValid()) { return false; }
	const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source"), TEXT("ThirdParty"),
		TEXT("litehtml"), TEXT("include"), TEXT("litehtml"), TEXT("string_id.h"));
	return FFileHelper::LoadFileToString(OutContents, *Path);
}

void ParseLitehtmlPropertyNames(const FString& StringIdH, TSet<FString>& Out)
{
	// The litehtml `// CSS property names` block.
	TArray<FString> Lines;
	StringIdH.ParseIntoArrayLines(Lines);
	bool bInBlock = false;
	for (const FString& Line : Lines)
	{
		const FString Trim = Line.TrimStartAndEnd();
		if (Trim.Contains(TEXT("// CSS property names"))) { bInBlock = true; continue; }
		if (!bInBlock) { continue; }
		if (Trim.StartsWith(TEXT("_")) && Trim.EndsWith(TEXT("_,")))
		{
			FString Id = Trim.LeftChop(2);                          // drop the trailing "_,"
			while (Id.StartsWith(TEXT("_"))) { Id = Id.RightChop(1); } // drop ALL leading "_" (__litehtml_ -> litehtml-)
			Out.Add(Id.Replace(TEXT("_"), TEXT("-")));              // _flex_direction_ -> "flex-direction"
		}
	}
}

const TArray<FString>& CssAuditedExclusions()
{
	// THE 7 AUDITED EXCLUSIONS (hand-curated; each guarded to be a real litehtml parse-name).
	// THE source for the codegen.
	static const TArray<FString> E = {
		TEXT("litehtml-border-spacing-x"), TEXT("litehtml-border-spacing-y"),
		TEXT("background-image-baseurl"), TEXT("list-style-image-baseurl"),
		TEXT("font-variant"), TEXT("counter-reset"), TEXT("counter-increment"),
	};
	return E;
}

const TArray<FString>& CssCathodeComputed()
{
	// THE 2 cathode_computed props — Axis-3-IN but NOT litehtml parse-names (opacity, letter-spacing).
	// The named +2 exception; guarded so a 3rd silent non-litehtml prop flags.
	static const TArray<FString> E = { TEXT("opacity"), TEXT("letter-spacing"), };
	return E;
}

void BuildCssAllowListSet(const FString& StringIdH, TArray<FString>& OutSorted)
{
	TSet<FString> Set;
	ParseLitehtmlPropertyNames(StringIdH, Set);
	for (const FString& Ex : CssAuditedExclusions()) { Set.Remove(Ex); } // - the 7
	for (const FString& C : CssCathodeComputed()) { Set.Add(C); }        // + the 2
	OutSorted = Set.Array();
	OutSorted.Sort(); // deterministic order so the generated artifacts are stable (regen-vs-committed exact)
}

FString GenerateCssAllowListToml()
{
	FString StringIdH;
	if (!LoadLitehtmlStringIdH(StringIdH)) { return FString(); }
	TArray<FString> Set;
	BuildCssAllowListSet(StringIdH, Set);
	FString Out;
	Out += TEXT("# css-allow-list.toml -- the Cathode authoring CSS allow-list (GENERATED -- do not hand-edit).\n");
	Out += TEXT("# 025 S2 (Model A): the supported CSS-property set PROJECTED from litehtml's string_id.h parse-names\n");
	Out += TEXT("# MINUS the 7 audited exclusions PLUS the 2 cathode_computed (opacity, letter-spacing). Regenerate via the\n");
	Out += TEXT("# regenerate-mode test; the coherence test asserts this committed file == the regeneration off litehtml.\n");
	Out += TEXT("# The CSS linter validates authored CSS against this list.\n");
	Out += TEXT("[properties]\n");
	Out += TEXT("allow = [\n");
	for (const FString& Name : Set) { Out += TEXT("    \"") + Name + TEXT("\",\n"); }
	Out += TEXT("]\n");
	return Out;
}

FString GenerateCssAllowListCpp()
{
	FString StringIdH;
	if (!LoadLitehtmlStringIdH(StringIdH)) { return FString(); }
	TArray<FString> Set;
	BuildCssAllowListSet(StringIdH, Set);
	FString Out;
	Out += TEXT("// CathodeCssAllowListGenerated.cpp -- the baked-in CSS allow-list (GENERATED -- do not hand-edit).\n");
	Out += TEXT("// 025 S2 (Model A): litehtml string_id.h parse-names -7 audited exclusions +2 cathode_computed.\n");
	Out += TEXT("// Regenerate via the regenerate-mode test (coherence-guarded). string_id.h is not on disk at runtime, so\n");
	Out += TEXT("// the runtime IsDeclaredCssProperty (S3) reads THIS baked array (the hand accessor decl is in the .h).\n");
	Out += TEXT("#include \"CathodeCssAllowListGenerated.h\"\n\n");
	Out += TEXT("namespace Cathode\n{\n\n");
	Out += TEXT("static const TCHAR* const GCssAllowList[] =\n{\n");
	for (const FString& Name : Set) { Out += TEXT("\tTEXT(\"") + Name + TEXT("\"),\n"); }
	Out += TEXT("};\n\n");
	Out += TEXT("TArray<FString> GetGeneratedCssAllowListNames()\n{\n");
	Out += TEXT("\tTArray<FString> Names;\n");
	Out += TEXT("\tNames.Reserve(UE_ARRAY_COUNT(GCssAllowList));\n");
	Out += TEXT("\tfor (const TCHAR* N : GCssAllowList) { Names.Add(N); }\n");
	Out += TEXT("\treturn Names;\n");
	Out += TEXT("}\n\n");
	Out += TEXT("} // namespace Cathode\n");
	return Out;
}

} // namespace Cathode

#endif // WITH_DEV_AUTOMATION_TESTS
