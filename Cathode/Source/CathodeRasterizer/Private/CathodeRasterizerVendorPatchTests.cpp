#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Containers/Array.h"

THIRD_PARTY_INCLUDES_START
#include "blend2d.h"
THIRD_PARTY_INCLUDES_END

// The Blend2D `bl_array_assign_data` under-alloc heap-overflow VERIFY (the crash->survive flip).
//
// THE BUG (vendored blend2d/core/array.cpp:883, asan-grounded): bl_array_assign_data sized the new impl from
// the array's OLD size (`u.size` — 0 for a fresh array), NOT the new element count `n`, then copied n*item_size bytes ->
// a heap-buffer-overflow on ANY copy INTO a fresh BLArray. Fixed by patches/blend2d-assign-data-underalloc.patch (size
// from `n`), applied by the setup.ps1 `git apply` hook BEFORE the blend2d build.
//
// THE PROOF SHAPE: drive `assign_data` DIRECTLY — a copy into a FRESH BLArray — NOT the external-data
// bypass (CathodeRasterBlend2D.cpp:66, which never calls the buggy path and so survives UNPATCHED too -> a vacuous green).
// On the UNPATCHED lib this OVERFLOWS at assign_data: the process crashes mid-test (or the corrupt round-trip fails) and the
// SURVIVED marker NEVER emits. On the PATCHED lib assign_data survives, the data round-trips byte-exact, and the marker
// EMITS. The marker's presence in the log IS the flip ("Target up to date" is NOT freshness — a stale unpatched lib can't
// complete + emit). N is large (512 KiB) so the unpatched overflow is unmistakable (the asan repro was a 410712-byte copy).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeBlend2DAssignDataSurvivesTest,
	"Cathode.Rasterizer.Vendor.Blend2DAssignDataSurvives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeBlend2DAssignDataSurvivesTest::RunTest(const FString& Parameters)
{
	// A copy INTO A FRESH BLArray (capacity 0). The fresh array's impl was sized from 0, so on the unpatched lib the
	// n-byte copy overflows that tiny buffer by ~N. (The sibling bl_array_reserve sizes from n correctly — this is the
	// one path that didn't.)
	const size_t N = 512 * 1024; // 512 KiB
	TArray<uint8> Src;
	Src.SetNumUninitialized((int32)N);
	for (int32 i = 0; i < (int32)N; ++i) { Src[i] = (uint8)(i & 0xFF); }

	BLArray<uint8_t> Arr; // fresh, capacity 0
	const BLResult R = Arr.assign_data(Src.GetData(), N); // UNPATCHED: heap-overflow HERE (the process crashes). PATCHED: survives.

	// --- reached cleanly only on the PATCHED lib (the unpatched crashed at assign_data above) ---
	TestEqual(TEXT("[029-S1] assign_data into a fresh array returns BL_SUCCESS"), (int32)R, (int32)BL_SUCCESS);
	TestEqual(TEXT("[029-S1] the array size == N (sized from the NEW count, not the old 0)"), (int32)Arr.size(), (int32)N);

	// Byte-exact round-trip: on the patched lib the buffer was sized for N, so every element reads back; a stale unpatched
	// lib that somehow didn't crash would have a tiny buffer -> garbage past it -> this fails (the second discriminator).
	bool bRoundTrip = (Arr.size() == N);
	for (int32 i = 0; i < (int32)N && bRoundTrip; ++i) { if (Arr[(size_t)i] != (uint8_t)(i & 0xFF)) { bRoundTrip = false; } }
	TestTrue(TEXT("[029-S1] the copied data round-trips byte-exact (N bytes, no overflow/truncation)"), bRoundTrip);

	// THE FLIP MARKER — its presence in the log == the PATCHED bl_array_assign_data drove a fresh-array copy and SURVIVED.
	AddInfo(FString::Printf(
		TEXT("[029-S1] Blend2D assign_data SURVIVED: a fresh-array copy of %d bytes -> BL_SUCCESS + byte-exact round-trip ")
		TEXT("(patched: sizes from n, not the old u.size) -- crash->survive flip, driving the buggy path NOT the bypass"),
		(int32)N));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
