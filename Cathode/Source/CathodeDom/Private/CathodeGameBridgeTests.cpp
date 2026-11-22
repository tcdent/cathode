#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeGameBridge.h"      // ICathodeGameBridge, CreateStubGameBridge, InstallGameBridgeAsGlobal
#include "CathodeJsRuntime.h"       // ICathodeJsRuntime (GetNativeContextHandle)
#include "CathodeRuntimeTesting.h"  // FScopedTestRuntime (the production runtime, single-instance claim)

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace
{
using namespace Cathode;

// Eval a JS expression yielding an int (the reachability/value oracle — comparisons return 1/0).
int32 EvalInt(JSContext* Ctx, const char* Src)
{
	JSValue Result = JS_Eval(Ctx, Src, FCStringAnsi::Strlen(Src), "<game-bridge-test>", JS_EVAL_TYPE_GLOBAL);
	int32 Out = -1;
	JS_ToInt32(Ctx, &Out, Result);
	JS_FreeValue(Ctx, Result);
	return Out;
}

// A SECOND backing, DISTINCT from the stub (the stub-swap target). Different values so a bridge hardcoded to the stub
// would keep returning the stub's 42/'stub:x' after the swap -> FAIL.
class FSwapGameBridge final : public ICathodeGameBridge
{
public:
	virtual int32 GetTickCount() const override { return 99; }
	virtual bool GetEntityName(const FString& Id, FString& OutName) const override { OutName = TEXT("swapped:") + Id; return true; }
};
} // namespace

// The game.* HOST BRIDGE reachability + stub-swap proof. InstallGameBridgeAsGlobal is the PRODUCTION install
// function the composer calls (CathodeCompositionOwner, beside FDomDocument::InstallAsGlobal); this drives it directly on a
// real runtime (the document-precedent test shape — DOM tests call Doc->InstallAsGlobal() directly too). The codegen
// coherence (Cathode.Authoring.Dts.Coherence/.Guards, which cover game) proves the OTHER half: the .d.ts game.* ==
// the GGameMembers table the install reads -> reachability == .d.ts BY CONSTRUCTION (one table, no second list).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeGameBridgeReachabilityTest,
	"Cathode.GameBridge.Reachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeGameBridgeReachabilityTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid() || !Runtime->IsValid()) { AddError(TEXT("[029-#28] runtime failed to start")); return false; }
	JSContext* const Ctx = static_cast<JSContext*>(Runtime->GetNativeContextHandle());
	if (Ctx == nullptr) { AddError(TEXT("[029-#28] runtime exposes no native context")); return false; }

	// Install the STUB backing via the PRODUCTION install (the SAME InstallGameBridgeAsGlobal the composer call-site uses).
	TUniquePtr<ICathodeGameBridge> Stub = CreateStubGameBridge();
	InstallGameBridgeAsGlobal(Ctx, *Stub);

	// REACHABILITY — game.* is on the live context; BOTH member kinds reachable; the stub's values surface through the bind.
	TestEqual(TEXT("[029-#28] game is installed as an object global"), EvalInt(Ctx, "typeof game === 'object' && game !== null ? 1 : 0"), 1);
	TestEqual(TEXT("[029-#28] game.tickCount is a property reading the backing (stub == 42)"), EvalInt(Ctx, "game.tickCount === 42 ? 1 : 0"), 1);
	TestEqual(TEXT("[029-#28] game.getEntityName is a method"), EvalInt(Ctx, "typeof game.getEntityName === 'function' ? 1 : 0"), 1);
	TestEqual(TEXT("[029-#28] game.getEntityName(id) reads the ARG + the backing (=> 'stub:x')"), EvalInt(Ctx, "game.getEntityName('x') === 'stub:x' ? 1 : 0"), 1);

	// STUB-SWAP — hand in a DIFFERENT backing (the bridge is NOT hardcoded to the stub) -> game.* calls IT (distinct values).
	FSwapGameBridge Swapped;
	InstallGameBridgeAsGlobal(Ctx, Swapped);
	TestEqual(TEXT("[029-#28] STUB-SWAP: game.tickCount now reads the SWAPPED backing (99, not the stub's 42)"), EvalInt(Ctx, "game.tickCount === 99 ? 1 : 0"), 1);
	TestEqual(TEXT("[029-#28] STUB-SWAP: game.getEntityName now reads the SWAPPED backing ('swapped:x', not 'stub:x')"), EvalInt(Ctx, "game.getEntityName('x') === 'swapped:x' ? 1 : 0"), 1);
	// The discriminator: a bridge hardcoded to the stub stays 42/'stub:x' here. The flip to 99/'swapped:x' proves the
	// install binds the HANDED-IN backing -> "the bridge works when a real object is handed to it".

	AddInfo(TEXT("[029-#28] game.* host bridge: reachable on the live context, both member kinds bound to the backing, stub-swappable"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
