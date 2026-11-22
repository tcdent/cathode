#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeResolver.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Containers/StringConv.h"
#include "Interfaces/IPluginManager.h"

// The PRODUCTION read-mechanism IBundleSource (CreateUEFileBundleSource): a GENUINE FFileHelper disk
// read of a committed fixture (NOT the in-memory FMemoryBundleSource) — see COVERAGE.md Deferred #3.
//   - byte-EXACT: OutBytes == a HARDCODED distinct marker authored HERE -- NON-circular (NOT a 2nd FFileHelper read /
//     re-seed, which would match its own wrong interpretation); NOT Contains (a Contains passes a partial/over-read).
//   - STABLE FIXTURE BYTES (the Windows-checkout trap): the fixture is a SINGLE-LINE, NO-trailing-newline, ASCII
//     marker (35 bytes) -> NO CRLF/LF/BOM ambiguity (git autocrlf could otherwise check out LF-as-CRLF -> a CORRECT
//     read false-REDs); a scoped .gitattributes marks probe.txt -text (verbatim checkout). The content is
//     NON-echoable from the bundle path ('probe.txt').
//   - clean miss: Read(<nonexistent>) -> false AND OutBytes empty (no partial-fill/garbage; no crash).
//   - the RootDir is TEST-SUPPLIED (no baked default-root); the fixture is located via IPluginManager GetBaseDir (the
//     MountProof/Verify/CssCapability convention) -> on disk at run time (the MountProof precedent proves it resolves).

namespace
{
using namespace Cathode;

// The committed fixture's EXACT content -- the HARDCODED, non-circular byte-exact oracle. Single line, ASCII, 35
// bytes, NO trailing newline. Lives at <plugin>/Source/CathodeResolver/Private/Tests/Fixtures/probe.txt.
const TCHAR* GFixtureBundlePath = TEXT("probe.txt");
const TCHAR* GFixtureContent    = TEXT("CATHODE-019-RESOLVER-FIXTURE-7Q2X9F"); // 35 ASCII bytes; non-echoable from the path

FString FixturesRootDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	return Plugin.IsValid() ? (Plugin->GetBaseDir() / TEXT("Source/CathodeResolver/Private/Tests/Fixtures")) : FString();
}

// The hardcoded marker's exact UTF-8 bytes (== ASCII here) — the discriminating oracle, NOT a second disk read.
TArray<uint8> HardcodedExpectedBytes()
{
	const FTCHARToUTF8 Conv(GFixtureContent);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
	return Bytes;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeResolverFileSourceTest,
	"Cathode.Resolver.FileSource.RealBytes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeResolverFileSourceTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	const FString RootDir = FixturesRootDir();
	if (RootDir.IsEmpty()) { AddError(TEXT("could not locate the Cathode plugin base dir (IPluginManager)")); return false; }

	// Precondition: the committed fixture is on disk at run time (the GetBaseDir convention; the MountProof precedent).
	const FString FixtureFullPath = FPaths::Combine(RootDir, FString(GFixtureBundlePath));
	if (!FPaths::FileExists(FixtureFullPath))
	{
		AddError(FString::Printf(TEXT("the committed fixture is not on disk at run time: %s"), *FixtureFullPath));
		return false;
	}

	// The PRODUCTION backend, rooted at the fixtures dir (TEST-SUPPLIED RootDir — no baked default-root).
	TUniquePtr<IBundleSource> Source = CreateUEFileBundleSource(RootDir);

	// (1) byte-EXACT: a GENUINE FFileHelper disk read of the NEW backend -> OutBytes == the HARDCODED marker
	// (non-circular; NOT Contains; not echoable; not the in-memory source; not CRLF-drifted).
	const TArray<uint8> Expected = HardcodedExpectedBytes();
	TArray<uint8> OutBytes;
	const bool bHit = Source->Read(GFixtureBundlePath, OutBytes);
	TestTrue(TEXT("(hit) the production backend reads the committed fixture (genuine FFileHelper disk read)"), bHit);
	TestTrue(FString::Printf(TEXT("(byte-exact) OutBytes (%d bytes) == the hardcoded distinct marker (%d bytes) — not Contains/echo/in-memory/CRLF-drift"), OutBytes.Num(), Expected.Num()),
		OutBytes == Expected);

	// (2) CLEAN MISS: a missing path -> Read false AND OutBytes empty (the hit/miss contract; no partial/garbage/crash).
	TArray<uint8> MissBytes;
	const bool bMiss = Source->Read(TEXT("does-not-exist-9Z3K.bin"), MissBytes);
	TestFalse(TEXT("(miss) a missing path -> Read returns false (clean miss)"), bMiss);
	TestEqual(TEXT("(miss) OutBytes empty on a miss (no partial-fill/garbage)"), MissBytes.Num(), 0);

	return true;
}

// THE RESOLVER OVER THE PRODUCTION BACKEND (the goal: "real asset bytes load THROUGH the resolver"
// end-to-end). Test-only (CreateResourceResolver + CreateUEFileBundleSource both reused — no extra production
// code). The test above proves Read directly; this one proves the RESOLVER routes a URL through the PRODUCTION backend to real
// bytes: ResolveBytes(url, baseUrl) -> ResolveUrlPath normalizes the url to the root-relative path -> the production
// FFileHelper Read -> the real fixture bytes. byte-EXACT against the SAME hardcoded marker (non-circular, full-array ==),
// through the production source (NOT the in-memory FMemoryBundleSource).
// The URL is NON-TRIVIAL (genuinely exercises ResolveUrlPath, CathodeResolver.cpp:48-61 — NOT an identity pass-through):
// "../probe.txt" vs base "sub/index.html" -> BaseDir="sub/" -> "sub/../probe.txt" -> CollapseSegments -> "probe.txt".
// A resolver skipping base-resolution -> "../probe.txt" (wrong) -> miss -> RED; skipping the ".." collapse ->
// "sub/../probe.txt" (wrong) -> miss -> RED. URL != resolved path (a genuine normalization, not the backend's job).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeResolverOverProductionTest,
	"Cathode.Resolver.FileSource.ResolverOverProduction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeResolverOverProductionTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	const FString RootDir = FixturesRootDir();
	if (RootDir.IsEmpty()) { AddError(TEXT("could not locate the Cathode plugin base dir (IPluginManager)")); return false; }
	const FString FixtureFullPath = FPaths::Combine(RootDir, FString(GFixtureBundlePath));
	if (!FPaths::FileExists(FixtureFullPath))
	{
		AddError(FString::Printf(TEXT("the committed fixture is not on disk at run time: %s"), *FixtureFullPath));
		return false;
	}

	// The RESOLVER over the PRODUCTION backend (both reused; no new production code). Source outlives Resolver (the
	// resolver holds a reference) — Source declared first, destroyed last.
	TUniquePtr<IBundleSource> Source = CreateUEFileBundleSource(RootDir);
	TUniquePtr<IResourceResolver> Resolver = CreateResourceResolver(*Source);

	const TArray<uint8> Expected = HardcodedExpectedBytes(); // the SAME hardcoded oracle (non-circular)

	// (1) byte-EXACT END-TO-END: a NON-TRIVIAL URL "../probe.txt" vs base "sub/index.html" -> BaseDir="sub/" +
	// relative-join + ".." collapse -> "probe.txt" -> the PRODUCTION FFileHelper Read -> the marker. NOT the in-memory
	// source; NOT Contains; not echoable (URL "../probe.txt" != the resolved path "probe.txt"). Bites a stub backend OR a
	// skipped base-resolution ("../probe.txt" -> miss) OR a skipped ".." collapse ("sub/../probe.txt" -> miss).
	{
		TArray<uint8> OutBytes;
		const bool bHit = Resolver->ResolveBytes(TEXT("../probe.txt"), TEXT("sub/index.html"), OutBytes);
		TestTrue(TEXT("(hit) the resolver routes a NON-TRIVIAL URL THROUGH the production backend to real bytes"), bHit);
		TestTrue(FString::Printf(TEXT("(byte-exact end-to-end) ResolveBytes('../probe.txt','sub/index.html') -> OutBytes (%d bytes) == the hardcoded marker (%d bytes) through the PRODUCTION source (not in-memory; genuine BaseDir+collapse normalization -> 'probe.txt')"), OutBytes.Num(), Expected.Num()),
			OutBytes == Expected);
	}

	// (2) MISS-URL: a URL resolving to a nonexistent path -> the URL-namespace + the production backend's clean miss propagate -> false.
	{
		TArray<uint8> OutBytes;
		const bool bMiss = Resolver->ResolveBytes(TEXT("/does-not-exist-9Z3K.bin"), TEXT("pages/x.html"), OutBytes);
		TestFalse(TEXT("(miss-url) a URL resolving to a nonexistent path -> ResolveBytes false (clean miss propagates through the resolver)"), bMiss);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
