#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeResourceRouting.h"
#include "CathodeResolver.h"

#include "Misc/AutomationTest.h"

// Specs for the CathodeLayout container -> resolver routing (litehtml-FREE, the CathodePaintRouting pattern). Drive the
// routing helpers directly with a STUB IResourceResolver (input isolation — no litehtml): assert load_image ->
// ResolveBytes, import_css -> ResolveText, and a null resolver yields the not-found result.
// get_image_size -> RouteImageSize drives the REAL CathodeRasterizer Blend2D image-info decode: a real
// PNG fixture decodes to its intrinsic 7x3 (oracle = the image's OWN dims, not hardcoded in the path), and
// non-image bytes decode to 0x0 (negative control). Still litehtml-FREE.

namespace
{
using namespace Cathode;

// A real 7x3, 8-bit-grayscale PNG (generated; exact bytes). The REAL Blend2D decode must return 7x3 from these
// bytes — the oracle: the image's intrinsic dims, NOT hardcoded in the RouteImageSize path, so a faked or
// constant decode FAILS. (Distinctive non-square 7x3 also catches a width/height transposition.)
static const uint8 GTinyPng7x3[] = {
	0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
	0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 0x08, 0x00, 0x00, 0x00, 0x00, 0x7A, 0xA8, 0x4A,
	0x19, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x60, 0x90, 0xB3, 0x89,
	0xAA, 0x98, 0xB6, 0x85, 0x01, 0x8D, 0x06, 0x00, 0x4E, 0xD8, 0x07, 0x63, 0x7D, 0x4E, 0x81, 0x0E,
	0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

// A stub resolver scripting hits by URL (the helper's normalization is tested in CathodeResolver; here we
// test the container's DELEGATION to the resolver).
class FStubResolver final : public IResourceResolver
{
public:
	TMap<FString, FString> Files;          // url -> text content (UTF-8 on resolve)
	TMap<FString, TArray<uint8>> RawFiles; // url -> raw bytes (e.g. a real encoded image)
	mutable int32 ByteCalls = 0;

	virtual bool ResolveBytes(const FString& Url, const FString& /*BaseUrl*/, TArray<uint8>& OutBytes) const override
	{
		++ByteCalls;
		if (const TArray<uint8>* Raw = RawFiles.Find(Url))
		{
			OutBytes.Append(*Raw);
			return true;
		}
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
	Resolver.RawFiles.Add(TEXT("real.png"), TArray<uint8>(GTinyPng7x3, UE_ARRAY_COUNT(GTinyPng7x3)));

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

	// get_image_size -> RouteImageSize: routes to the resolver, then DECODES the bytes' intrinsic dimensions via
	// the REAL CathodeRasterizer Blend2D image-info decode.
	{
		// A real PNG decodes to its OWN dims — 7x3 comes from the real decode of the fixture bytes, NOT a
		// constant in the path (the oracle: a faked/constant decode FAILS these).
		Resolver.ByteCalls = 0;
		int32 W = -1, H = -1;
		RouteImageSize(&Resolver, TEXT("real.png"), TEXT(""), W, H);
		TestTrue(TEXT("get_image_size routed to the resolver"), Resolver.ByteCalls == 1);
		TestEqual(TEXT("real PNG decodes to its intrinsic WIDTH (7)"), W, 7);
		TestEqual(TEXT("real PNG decodes to its intrinsic HEIGHT (3)"), H, 3);

		// Negative control: non-image bytes ("PNGDATA") do NOT decode -> 0x0. Proves it is REALLY decoding (a
		// faked always-7x3 would wrongly report 7x3 here; a real decoder rejects the garbage).
		int32 GW = -1, GH = -1;
		RouteImageSize(&Resolver, TEXT("logo.png"), TEXT(""), GW, GH);
		TestTrue(TEXT("non-image bytes do not decode -> 0x0"), GW == 0 && GH == 0);
	}

	// Null resolver (no resolver wired): miss on every path, no crash.
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
