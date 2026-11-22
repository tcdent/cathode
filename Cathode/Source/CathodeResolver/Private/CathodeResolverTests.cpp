#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeResolver.h"

#include "Misc/AutomationTest.h"

// Specs for the BESPOKE URL-namespace logic + resolution over a neutral in-memory IBundleSource (INPUT
// isolation — no real FS / UE asset). Assert OUR bookkeeping: normalization results + resolve hit/miss.
// ZERO real-FS / real-UE-asset / real-decoded-bytes assertions.

namespace
{
using namespace Cathode;

// An in-memory bundle source keyed by root-relative path (what ResolveUrlPath produces).
class FMemoryBundleSource final : public IBundleSource
{
public:
	TMap<FString, TArray<uint8>> Files;

	void Put(const FString& Path, const FString& Utf8)
	{
		const FTCHARToUTF8 Conv(*Utf8);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
		Files.Add(Path, MoveTemp(Bytes));
	}

	virtual bool Read(const FString& BundlePath, TArray<uint8>& OutBytes) const override
	{
		if (const TArray<uint8>* Found = Files.Find(BundlePath))
		{
			OutBytes = *Found;
			return true;
		}
		return false; // clean miss
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeResolverUrlTest,
	"Cathode.Resolver.Url.Normalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeResolverUrlTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// Relative vs the base's directory.
	TestEqual(TEXT("relative, no base"), ResolveUrlPath(TEXT("logo.png"), TEXT("")), FString(TEXT("logo.png")));
	TestEqual(TEXT("relative vs base dir"), ResolveUrlPath(TEXT("logo.png"), TEXT("pages/index.html")), FString(TEXT("pages/logo.png")));
	TestEqual(TEXT("@import vs stylesheet base"), ResolveUrlPath(TEXT("theme.css"), TEXT("css/main.css")), FString(TEXT("css/theme.css")));

	// '/'-rooted absolute -> the virtual root (base ignored).
	TestEqual(TEXT("absolute vs root"), ResolveUrlPath(TEXT("/img/logo.png"), TEXT("pages/index.html")), FString(TEXT("img/logo.png")));

	// Collapse ./ and ../.
	TestEqual(TEXT("collapse ./"), ResolveUrlPath(TEXT("./a.png"), TEXT("pages/index.html")), FString(TEXT("pages/a.png")));
	TestEqual(TEXT("collapse ../"), ResolveUrlPath(TEXT("../shared/x.css"), TEXT("pages/sub/index.html")), FString(TEXT("pages/shared/x.css")));

	// ".." clamps at the root (trusted-only, no traversal escape).
	TestEqual(TEXT("../ clamps at root"), ResolveUrlPath(TEXT("../../../etc"), TEXT("a/b.html")), FString(TEXT("etc")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeResolverResolveTest,
	"Cathode.Resolver.Resolve.HitMiss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeResolverResolveTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FMemoryBundleSource Source;
	Source.Put(TEXT("img/logo.png"), TEXT("PNGDATA"));
	Source.Put(TEXT("css/main.css"), TEXT("body{}"));
	TUniquePtr<IResourceResolver> Resolver = CreateResourceResolver(Source);

	// ResolveBytes: a relative URL against a doc base -> root-relative -> hit.
	{
		TArray<uint8> Bytes;
		TestTrue(TEXT("image resolves (hit)"), Resolver->ResolveBytes(TEXT("logo.png"), TEXT("img/index.html"), Bytes));
		TestEqual(TEXT("resolved bytes carry the bundle content"), Bytes.Num(), 7); // "PNGDATA"
	}
	// Miss -> clean false.
	{
		TArray<uint8> Bytes;
		TestFalse(TEXT("missing resource misses cleanly"), Resolver->ResolveBytes(TEXT("missing.png"), TEXT(""), Bytes));
	}
	// ResolveText: linked CSS -> UTF-8 text.
	{
		FString Css;
		TestTrue(TEXT("linked css resolves as text"), Resolver->ResolveText(TEXT("main.css"), TEXT("css/index.html"), Css));
		TestEqual(TEXT("css text decoded"), Css, FString(TEXT("body{}")));
	}
	{
		FString Css;
		TestFalse(TEXT("missing css misses cleanly"), Resolver->ResolveText(TEXT("nope.css"), TEXT(""), Css));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
