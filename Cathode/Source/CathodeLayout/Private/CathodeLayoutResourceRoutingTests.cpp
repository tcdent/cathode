#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeResourceRouting.h"
#include "CathodeResolver.h"

#include "Misc/AutomationTest.h"

// Specs for the 005 container -> resolver routing (litehtml-FREE, the CathodePaintRouting pattern). Drive the
// routing helpers directly with a STUB IResourceResolver (input isolation — no litehtml, no real bytes):
// assert load_image -> ResolveBytes, import_css -> ResolveText, get_image_size -> routes-but-0x0, and a null
// resolver yields the pre-hook not-found result. ZERO litehtml / real-FS / real-decode assertions. Authored,
// not run this sprint.

namespace
{
using namespace Cathode;

// A stub resolver scripting hits by URL (the helper's normalization is tested in CathodeResolver; here we
// test the container's DELEGATION to the resolver).
class FStubResolver final : public IResourceResolver
{
public:
	TMap<FString, FString> Files; // url -> content
	mutable int32 ByteCalls = 0;

	virtual bool ResolveBytes(const FString& Url, const FString& /*BaseUrl*/, TArray<uint8>& OutBytes) const override
	{
		++ByteCalls;
		if (const FString* Content = Files.Find(Url))
		{
			const FTCHARToUTF8 Conv(**Content);
			OutBytes.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
			return true;
		}
		return false;
	}

	virtual bool ResolveText(const FString& Url, const FString& /*BaseUrl*/, FString& OutText) const override
	{
		if (const FString* Content = Files.Find(Url))
		{
			OutText = *Content;
			return true;
		}
		return false;
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutResourceRoutingTest,
	"Cathode.Layout.Resource.ContainerRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutResourceRoutingTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FStubResolver Resolver;
	Resolver.Files.Add(TEXT("logo.png"), TEXT("PNGDATA"));
	Resolver.Files.Add(TEXT("main.css"), TEXT("body{}"));

	// load_image -> RouteImageBytes -> ResolveBytes (hit / miss).
	{
		TArray<uint8> Bytes;
		TestTrue(TEXT("load_image routes to ResolveBytes (hit)"), RouteImageBytes(&Resolver, TEXT("logo.png"), TEXT(""), Bytes));
		TestEqual(TEXT("resolved image bytes"), Bytes.Num(), 7);
		TArray<uint8> Miss;
		TestFalse(TEXT("missing image -> clean miss"), RouteImageBytes(&Resolver, TEXT("nope.png"), TEXT(""), Miss));
	}

	// import_css -> RouteLinkedCss -> ResolveText (hit / miss).
	{
		FString Css;
		TestTrue(TEXT("import_css routes to ResolveText (hit)"), RouteLinkedCss(&Resolver, TEXT("main.css"), TEXT(""), Css));
		TestEqual(TEXT("resolved linked css text"), Css, FString(TEXT("body{}")));
		FString MissCss;
		TestFalse(TEXT("missing css -> clean miss"), RouteLinkedCss(&Resolver, TEXT("nope.css"), TEXT(""), MissCss));
	}

	// get_image_size -> RouteImageSize: routes to the resolver but returns 0x0 (size = 003 decode, integration).
	{
		Resolver.ByteCalls = 0;
		int32 W = -1, H = -1;
		RouteImageSize(&Resolver, TEXT("logo.png"), TEXT(""), W, H);
		TestTrue(TEXT("get_image_size routed to the resolver"), Resolver.ByteCalls == 1);
		TestTrue(TEXT("get_image_size returns 0x0 (size deferred to 003)"), W == 0 && H == 0);
	}

	// Null resolver == the pre-hook stub behavior (no resolver wired): miss on every path, no crash.
	{
		TArray<uint8> Bytes;
		TestFalse(TEXT("null resolver: image miss"), RouteImageBytes(nullptr, TEXT("logo.png"), TEXT(""), Bytes));
		FString Css;
		TestFalse(TEXT("null resolver: css miss"), RouteLinkedCss(nullptr, TEXT("main.css"), TEXT(""), Css));
		int32 W = -1, H = -1;
		RouteImageSize(nullptr, TEXT("logo.png"), TEXT(""), W, H);
		TestTrue(TEXT("null resolver: size 0x0"), W == 0 && H == 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
