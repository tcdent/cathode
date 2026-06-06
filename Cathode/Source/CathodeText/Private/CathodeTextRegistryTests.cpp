#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeFontRegistry.h"
#include "CathodeText.h"

#include "Misc/AutomationTest.h"

// Specs for the font registry + single-load byte store — OUR bookkeeping, via an INJECTED recording
// loader (real UE file loading is NOT tested). They assert handle resolution, single-load (loader called
// once per handle), and shared-byte identity. The bytes are OPAQUE test input — never asserted as
// font-file content. Authored, not run this sprint. (hb_*/BL*-free: this slice is byte loading only.)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTextFontRegistryTest,
	"Cathode.Text.FontRegistry.SingleLoadAndHandles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTextFontRegistryTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	int32 TotalLoadCalls = 0;
	TMap<FString, int32> PerPathLoadCalls;

	FFontFileLoader Loader = [&TotalLoadCalls, &PerPathLoadCalls](const FString& Path, TArray<uint8>& OutBytes)
	{
		++TotalLoadCalls;
		PerPathLoadCalls.FindOrAdd(Path)++;
		OutBytes.Init(0, 8); // opaque scripted bytes (NOT a real font); only their length/identity matter
		return true;
	};

	FFontRegistry Registry(Loader);

	// Handle resolution: same path -> same handle; new path -> a new, distinct handle.
	const FFontHandle A1 = Registry.RegisterFontFile(TEXT("fonts/a.ttf"));
	const FFontHandle A2 = Registry.RegisterFontFile(TEXT("fonts/a.ttf"));
	const FFontHandle B = Registry.RegisterFontFile(TEXT("fonts/b.ttf"));

	TestTrue(TEXT("first handle is valid"), A1.IsValid());
	TestTrue(TEXT("same path returns the same handle"), A1 == A2);
	TestTrue(TEXT("a different path returns a different handle"), A1 != B);
	TestTrue(TEXT("the second font handle is valid"), B.IsValid());

	// Single-load: the loader is called once per UNIQUE path; the repeat lookup reused, no reload.
	TestEqual(TEXT("loader called once per unique path (2 paths)"), TotalLoadCalls, 2);
	TestEqual(TEXT("the repeated path loaded exactly once"), PerPathLoadCalls.FindRef(TEXT("fonts/a.ttf")), 1);

	// Shared-byte identity: the same handle yields the SAME bytes view (stable data pointer); different
	// handles yield different bytes. (Identity is registry bookkeeping — not the font's contents.)
	TArrayView<const uint8> A_First;
	TArrayView<const uint8> A_Second;
	TArrayView<const uint8> B_Bytes;
	TestTrue(TEXT("bytes resolve for handle A"), Registry.GetFontBytes(A1, A_First));
	TestTrue(TEXT("bytes resolve again for handle A"), Registry.GetFontBytes(A2, A_Second));
	TestTrue(TEXT("bytes resolve for handle B"), Registry.GetFontBytes(B, B_Bytes));

	TestTrue(TEXT("same handle -> same bytes pointer (single shared copy)"), A_First.GetData() == A_Second.GetData());
	TestEqual(TEXT("registry preserved the loaded byte count"), A_First.Num(), 8);
	TestTrue(TEXT("different handles -> different bytes"), A_First.GetData() != B_Bytes.GetData());

	// An invalid handle resolves to no bytes.
	TArrayView<const uint8> NoBytes;
	TestFalse(TEXT("an invalid handle has no bytes"), Registry.GetFontBytes(FFontHandle{}, NoBytes));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeTextFontRegistryLoadFailureTest,
	"Cathode.Text.FontRegistry.LoadFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeTextFontRegistryLoadFailureTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	// A loader that fails -> the registry yields an invalid handle and no bytes (no partial state).
	FFontFileLoader FailingLoader = [](const FString&, TArray<uint8>&) { return false; };
	FFontRegistry Registry(FailingLoader);

	const FFontHandle Handle = Registry.RegisterFontFile(TEXT("fonts/missing.ttf"));
	TestFalse(TEXT("a failed load yields an invalid handle"), Handle.IsValid());

	TArrayView<const uint8> NoBytes;
	TestFalse(TEXT("an invalid handle has no bytes"), Registry.GetFontBytes(Handle, NoBytes));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
