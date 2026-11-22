#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDiagnostics.h"
#include "CathodeGlobalEnvironment.h"
#include "CathodeRuntimeStartup.h"
#include "CathodeScheduler.h"

#include "Misc/AutomationTest.h"

// Specs for Runtime Core's BESPOKE orchestration: the global-environment ASSEMBLY order and the runtime
// STARTUP order. Each drives the EXACT production unit (FGlobalEnvironment::AssembleGlobals /
// FRuntimeStartup::Startup — the same the worker's Run() uses) and proves the order via a single shared
// ordered effect-log fed through recording seams (same discipline as the tick's phase order). No JS
// global is bound and no vendor binding is tested.

namespace
{
using namespace Cathode;

// Minimal sink — assembly only needs an ICathodeDiagnosticSink instance to deliver as the console target.
class FNoopSink final : public ICathodeDiagnosticSink
{
public:
	virtual void ReportError(const FJsError&) override {}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeAssemblyOrderTest,
	"Cathode.RuntimeCore.Assembly.GlobalAssemblyOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeAssemblyOrderTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	TArray<FString> Order;

	FNoopSink Sink;
	FCathodeScheduler Scheduler;

	// Capture what each wired seam receives, to prove console/timers get their REAL targets.
	ICathodeDiagnosticSink* ConsoleTarget = nullptr;
	FCathodeScheduler* TimerTarget = nullptr;

	FGlobalEnvironment Env;
	Env.OnInstallConsole = [&Order, &ConsoleTarget](ICathodeDiagnosticSink& S) { Order.Add(TEXT("console")); ConsoleTarget = &S; };
	Env.OnInstallTimers = [&Order, &TimerTarget](FCathodeScheduler& Sch) { Order.Add(TEXT("timers")); TimerTarget = &Sch; };
	Env.OnInstallGameBridge = [&Order]() { Order.Add(TEXT("game")); };
	Env.OnInstallDocument = [&Order]() { Order.Add(TEXT("document")); };

	Env.AssembleGlobals(Sink, Scheduler);

	const TArray<FString> Expected = { TEXT("console"), TEXT("timers"), TEXT("game"), TEXT("document") };
	TestEqual(TEXT("globals assembled in contract order"), Order.Num(), Expected.Num());
	if (Order.Num() == Expected.Num())
	{
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("assembly step %d"), Index), Order[Index], Expected[Index]);
		}
	}

	// Wired-to-real-targets: console received the actual sink, timers the actual scheduler.
	TestTrue(TEXT("console seam received the real diagnostic sink"), ConsoleTarget == &Sink);
	TestTrue(TEXT("timer seam received the real scheduler"), TimerTarget == &Scheduler);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeRuntimeStartupOrderTest,
	"Cathode.RuntimeCore.Lifecycle.StartupOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeRuntimeStartupOrderTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	TArray<FString> Order;

	FRuntimeStartup::FSteps Steps;
	Steps.CreateRuntimeContext = [&Order]() { Order.Add(TEXT("create")); };
	Steps.InstallSafetyBudgets = [&Order]() { Order.Add(TEXT("budgets")); };
	Steps.AssembleGlobals = [&Order]() { Order.Add(TEXT("assemble")); };
	Steps.LoadAuthorScript = [&Order]() { Order.Add(TEXT("load")); };
	Steps.BeginTicking = [&Order]() { Order.Add(TEXT("begin")); };

	FRuntimeStartup Startup(MoveTemp(Steps));
	Startup.Startup();

	const TArray<FString> Expected = {
		TEXT("create"), TEXT("budgets"), TEXT("assemble"), TEXT("load"), TEXT("begin")
	};
	TestEqual(TEXT("startup ran the steps in the doc order"), Order.Num(), Expected.Num());
	if (Order.Num() == Expected.Num())
	{
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("startup step %d"), Index), Order[Index], Expected[Index]);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
