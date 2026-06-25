#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CathodeAuthoringDts.h"     // GenerateAuthoringDts
#include "CathodeAuthoringTables.h"  // the FBoundMember accessors (for the Iface-coverage + type-resolve guards)
#include "CathodeBoundMember.h"      // FBoundMember
#include "CathodeContractGlobalNames.h"  // GetContractGlobalNames (the install set the globals guard binds to)
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"

// 025 S1 — the .d.ts codegen driver + guards. Two distinct roles (FORK driver-home (a)):
//  - REGENERATE (the DRIVER): writes lib.cathode.d.ts from GenerateAuthoringDts() — gated to regenerate-mode (the
//    CATHODE_REGEN_DTS env var), so a normal suite run does NOT overwrite the committed file. Run on-demand to update.
//  - COHERENCE (the GUARD): asserts the COMMITTED lib.cathode.d.ts == GenerateAuthoringDts() (the SAME projection off
//    the PRODUCTION tables) — every suite run; a binding add/remove flows through, a stale commit REDs.
//  - GUARDS: the 3 hand-section guards (globals names / Iface-coverage / referenced-type-resolve).
// 025 S1 — the regenerate-mode gate is a CVAR (env sets it via execute_script before the regenerate run); an env var
// was infeasible (env's editor-launch path injects none). Default 0 -> a normal suite run is a no-op (the coherence
// guard is NOT masked); set `cathode.RegenDts 1` to write.
static TAutoConsoleVariable<int32> CVarRegenDts(TEXT("cathode.RegenDts"), 0,
	TEXT("025 S1: set 1 + run Cathode.Authoring.Dts.Regenerate to write lib.cathode.d.ts from the binding tables."));

namespace
{
	using namespace Cathode;

	FString AuthoringDtsPath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
		return Plugin.IsValid() ? (Plugin->GetBaseDir() / TEXT("Authoring/lib.cathode.d.ts")) : FString();
	}

	// Gather every distinct FBoundMember Iface across the production tables.
	void GatherIfaces(TSet<FString>& OutIfaces)
	{
		const FBoundMember* M = nullptr; int32 C = 0;
		GetDomNodeBoundMembersForCodegen(M, C);          for (int32 i = 0; i < C; ++i) { OutIfaces.Add(ANSI_TO_TCHAR(M[i].Iface)); }
		GetDomDocumentBoundMembersForCodegen(M, C);      for (int32 i = 0; i < C; ++i) { OutIfaces.Add(ANSI_TO_TCHAR(M[i].Iface)); }
		GetDomEventBoundMembersForCodegen(M, C);         for (int32 i = 0; i < C; ++i) { OutIfaces.Add(ANSI_TO_TCHAR(M[i].Iface)); }
		GetDomKeyboardEventBoundMembersForCodegen(M, C); for (int32 i = 0; i < C; ++i) { OutIfaces.Add(ANSI_TO_TCHAR(M[i].Iface)); }
		GetGameBoundMembersForCodegen(M, C);             for (int32 i = 0; i < C; ++i) { OutIfaces.Add(ANSI_TO_TCHAR(M[i].Iface)); } // 029 #28
	}
}

// --- THE DRIVER (regenerate-mode; writes the file only when CATHODE_REGEN_DTS is set) ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCathodeAuthoringDtsRegenerateTest, "Cathode.Authoring.Dts.Regenerate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCathodeAuthoringDtsRegenerateTest::RunTest(const FString& Parameters)
{
	if (CVarRegenDts.GetValueOnGameThread() != 1)
	{
		AddInfo(TEXT("[025-S1] regenerate-mode OFF (cathode.RegenDts != 1) — not writing the committed .d.ts (normal-suite no-op)."));
		return true;
	}
	const FString Path = AuthoringDtsPath();
	if (Path.IsEmpty()) { AddError(TEXT("[025-S1] could not resolve the Cathode plugin dir for the .d.ts path")); return false; }
	const FString Generated = GenerateAuthoringDts();
	if (!FFileHelper::SaveStringToFile(Generated, *Path)) { AddError(FString::Printf(TEXT("[025-S1] failed to write %s"), *Path)); return false; }
	AddInfo(FString::Printf(TEXT("[025-S1] REGENERATED lib.cathode.d.ts (%d chars) -> %s"), Generated.Len(), *Path));
	return true;
}

// --- THE COHERENCE GUARD (committed == regenerated; every suite run) ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCathodeAuthoringDtsCoherenceTest, "Cathode.Authoring.Dts.Coherence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCathodeAuthoringDtsCoherenceTest::RunTest(const FString& Parameters)
{
	const FString Path = AuthoringDtsPath();
	if (Path.IsEmpty()) { AddError(TEXT("[025-S1] could not resolve the .d.ts path")); return false; }
	FString Committed;
	if (!FFileHelper::LoadFileToString(Committed, *Path)) { AddError(FString::Printf(TEXT("[025-S1] could not read the committed %s"), *Path)); return false; }
	const FString Regenerated = GenerateAuthoringDts();
	// Normalize CRLF (git autocrlf on the box) so the compare is content-exact, not EOL-sensitive.
	Committed.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	const bool bMatch = Committed.Equals(Regenerated, ESearchCase::CaseSensitive);
	AddInfo(FString::Printf(TEXT("[025-S1] regen-vs-committed: committed=%d chars regen=%d chars match=%d"), Committed.Len(), Regenerated.Len(), bMatch ? 1 : 0));
	TestTrue(TEXT("[025-S1] the committed lib.cathode.d.ts == the regeneration off the PRODUCTION FBoundMember tables (a binding add/remove flows through; a stale commit REDs)"), bMatch);
	return true;
}

// --- THE 3 GUARDS (globals names / Iface-coverage / referenced-type-resolve) ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCathodeAuthoringDtsGuardsTest, "Cathode.Authoring.Dts.Guards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCathodeAuthoringDtsGuardsTest::RunTest(const FString& Parameters)
{
	const FString Dts = GenerateAuthoringDts();

	// GUARD 1 — the globals, NON-CIRCULAR (bound to the install set): the .d.ts globals NAMES == GetContractGlobalNames()
	// (the CathodeContractGlobals install set, the single source) + document (the InstallAsGlobal documented known). A
	// CathodeContractGlobals add/remove flows through -> the .d.ts globals differ from the install set -> RED. + no
	// `// ERROR` (every install global has an authored signature).
	{
		TSet<FString> Expected;
		for (const FString& N : GetContractGlobalNames()) { Expected.Add(N); }
		Expected.Add(TEXT("document"));
		Expected.Add(TEXT("game")); // 029 #28 — the documented-known game.* global (InstallGameBridgeAsGlobal, the composer)
		TSet<FString> Declared;
		{
			TArray<FString> Lines; Dts.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				const FString T = Line.TrimStart();
				const TCHAR* Kw = T.StartsWith(TEXT("declare const ")) ? TEXT("declare const ") : (T.StartsWith(TEXT("declare function ")) ? TEXT("declare function ") : nullptr);
				if (Kw == nullptr) { continue; }
				const FString Rest = T.RightChop(FCString::Strlen(Kw));
				FString Name;
				for (TCHAR Ch : Rest) { if (FChar::IsAlnum(Ch) || Ch == TEXT('_')) { Name.AppendChar(Ch); } else { break; } }
				if (Name.Len() > 0) { Declared.Add(Name); }
			}
		}
		AddInfo(FString::Printf(TEXT("[025-S1] globals guard (non-circular): declared=%d expected(install-set)=%d"), Declared.Num(), Expected.Num()));
		TestTrue(TEXT("[025-S1] globals guard: the .d.ts globals == the install set (GetContractGlobalNames + document) — a CathodeContractGlobals add/remove flows through"),
			Declared.Difference(Expected).Num() == 0 && Expected.Difference(Declared).Num() == 0);
		TestFalse(TEXT("[025-S1] globals guard: every install global has an authored signature (no '// ERROR' in the .d.ts)"), Dts.Contains(TEXT("// ERROR")));
	}

	// GUARD 2 — Iface-coverage: every FBoundMember Iface has an emitted interface (a NEW engine interface flags — no
	// silent orphan: a binding on an Iface absent from the extends map would not emit `interface <Iface>`).
	TSet<FString> Ifaces; GatherIfaces(Ifaces);
	for (const FString& If : Ifaces)
	{
		TestTrue(FString::Printf(TEXT("[025-S1] Iface-coverage guard: the .d.ts emits 'interface %s' (the extends map covers it)"), *If),
			Dts.Contains(FString::Printf(TEXT("interface %s"), *If)));
	}

	// GUARD 3 — referenced-type-resolve: every Capitalized type token in a projected signature resolves to an FBoundMember
	// Iface OR a defined auxiliary type. A binding signature introducing a NEW referenced type (a Capitalized token
	// absent from both) flags — no silent undefined-type emit.
	// THE RESOLVABLE SET = every type DECLARED in the generated .d.ts (interface X / type X) — derived from the OUTPUT
	// itself, so a referenced type resolves iff it is actually declared (robust: no hardcoded aux list that could drift
	// from what the codegen emits).
	TSet<FString> Resolvable;
	{
		TArray<FString> Lines; Dts.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			const FString T = Line.TrimStart();
			const TCHAR* Kw = T.StartsWith(TEXT("interface ")) ? TEXT("interface ") : (T.StartsWith(TEXT("type ")) ? TEXT("type ") : nullptr);
			if (Kw == nullptr) { continue; }
			const FString Rest = T.RightChop(FCString::Strlen(Kw));
			FString Name;
			for (TCHAR Ch : Rest) { if (FChar::IsAlnum(Ch) || Ch == TEXT('_')) { Name.AppendChar(Ch); } else { break; } }
			if (Name.Len() > 0) { Resolvable.Add(Name); }
		}
	}
	const FBoundMember* M = nullptr; int32 C = 0;
	auto CheckTable = [&](const FBoundMember* Members, int32 Count)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			if (Members[i].Signature == nullptr) { continue; }
			const FString Sig = ANSI_TO_TCHAR(Members[i].Signature);
			// extract Capitalized identifiers (TS type names are Capitalized; primitives string/number/boolean/void/null are lowercase).
			FString Token;
			auto Flush = [&]()
			{
				if (Token.Len() > 0 && FChar::IsUpper(Token[0]))
				{
					TestTrue(FString::Printf(TEXT("[025-S1] resolve guard: signature type '%s' (member '%s') resolves to an Iface or aux type"), *Token, ANSI_TO_TCHAR(Members[i].JsName)),
						Resolvable.Contains(Token));
				}
				Token.Reset();
			};
			for (TCHAR Ch : Sig)
			{
				if (FChar::IsAlnum(Ch) || Ch == TEXT('_')) { Token.AppendChar(Ch); } else { Flush(); }
			}
			Flush();
		}
	};
	GetDomNodeBoundMembersForCodegen(M, C);          CheckTable(M, C);
	GetDomDocumentBoundMembersForCodegen(M, C);      CheckTable(M, C);
	GetDomEventBoundMembersForCodegen(M, C);         CheckTable(M, C);
	GetDomKeyboardEventBoundMembersForCodegen(M, C); CheckTable(M, C);
	GetGameBoundMembersForCodegen(M, C);             CheckTable(M, C); // 029 #28 — the game.* signatures resolve (Game, number, string)

	AddInfo(FString::Printf(TEXT("[025-S1] guards: %d FBoundMember Ifaces covered; globals + referenced types resolve"), Ifaces.Num()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
