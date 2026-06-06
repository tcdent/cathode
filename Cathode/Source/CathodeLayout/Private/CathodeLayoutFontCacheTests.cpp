#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeLayout.h"
#include "CathodeLayoutFontCache.h"

#include "Misc/AutomationTest.h"

// Spec for the bespoke font-handle cache — OUR bookkeeping, litehtml-free, via a recording ITextMeasurer.
// Asserts dedup (same font params -> same handle, one ResolveFont), distinct fonts -> distinct handles,
// and measurement dispatch through the handle. The measured width is the measurer's (vendor) value and is
// NOT asserted. Authored, not run this sprint.

namespace
{
using namespace Cathode;

class FRecordingMeasurer final : public ITextMeasurer
{
public:
	int32 ResolveCount = 0;
	int32 NextId = 100;
	FLayoutFontRef LastMeasuredFont;
	FString LastMeasuredText;

	virtual FLayoutFontRef ResolveFont(const FString&, int32, int32, bool) override
	{
		++ResolveCount;
		return FLayoutFontRef{ NextId++ };
	}
	virtual float MeasureWidth(const FString& Text, FLayoutFontRef Font) override
	{
		LastMeasuredText = Text;
		LastMeasuredFont = Font;
		return static_cast<float>(Text.Len()); // opaque; not asserted as a real width
	}
	virtual void GetFontMetrics(FLayoutFontRef, float& OutAscent, float& OutDescent, float& OutHeight) override
	{
		OutAscent = 8.0f;
		OutDescent = 2.0f;
		OutHeight = 10.0f;
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeLayoutFontCacheTest,
	"Cathode.Layout.FontCache.DedupAndDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeLayoutFontCacheTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FRecordingMeasurer Measurer;
	FLayoutFontCache Cache(Measurer);

	// Dedup: same params -> same handle, ResolveFont called once.
	const uint64 H1 = Cache.ResolveFont(TEXT("Arial"), 16, 400, false);
	const uint64 H2 = Cache.ResolveFont(TEXT("Arial"), 16, 400, false);
	TestTrue(TEXT("same font params -> same handle"), H1 == H2);
	TestEqual(TEXT("ResolveFont deduped to one call"), Measurer.ResolveCount, 1);
	TestTrue(TEXT("a valid handle is non-zero"), H1 != 0);

	// Distinct fonts -> distinct handles, each a fresh ResolveFont.
	const uint64 H3 = Cache.ResolveFont(TEXT("Arial"), 18, 400, false); // different size
	const uint64 H4 = Cache.ResolveFont(TEXT("Times"), 16, 400, false); // different face
	TestTrue(TEXT("different size -> different handle"), H3 != H1);
	TestTrue(TEXT("different face -> different handle"), H4 != H1 && H4 != H3);
	TestEqual(TEXT("three distinct fonts -> three ResolveFont calls"), Measurer.ResolveCount, 3);

	// Measurement dispatches through the handle's ref.
	Cache.MeasureWidth(H1, TEXT("hello"));
	TestTrue(TEXT("measure dispatched with the handle's font ref"), Measurer.LastMeasuredFont == Cache.GetRef(H1));
	TestEqual(TEXT("measure dispatched the text"), Measurer.LastMeasuredText, FString(TEXT("hello")));

	// Handle -> ref resolution.
	TestTrue(TEXT("GetRef of a valid handle is valid"), Cache.GetRef(H1).IsValid());
	TestFalse(TEXT("GetRef of the null handle is invalid"), Cache.GetRef(0).IsValid());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
