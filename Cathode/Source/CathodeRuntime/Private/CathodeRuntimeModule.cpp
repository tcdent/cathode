#include "CathodeRuntimeModule.h"

#include "CathodeBuildId.h"
#include "CathodeRuntimeWorker.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Build-coherence marker (sprint 010): a log category for the build signature below. The link-id mechanism
// (CathodeLinkTimeBuildId + the cross-module CathodeAggregateBuildId) lives in CathodeBuildId.cpp.
DEFINE_LOG_CATEGORY_STATIC(LogCathodeBuild, Log, All);

FCathodeRuntimeModule::~FCathodeRuntimeModule() = default;

void FCathodeRuntimeModule::StartupModule()
{
	// BUILD-COHERENCE MARKER: the [CATHODE-BUILD-SIGNATURE] line carries CathodeRuntime's OWN per-load link-id
	// (advances on ANY CathodeRuntime relink). NOTE: this is CathodeRuntime's per-load signature — emitted at
	// startup, before sibling Cathode modules are guaranteed loaded, so it canNOT aggregate them here. The
	// CROSS-MODULE coherence anchor is the faculty's coherence.buildId (Cathode::CathodeAggregateBuildId(),
	// read at faculty time when all Cathode modules are loaded). The "(module-TU compiled ...)" suffix is THIS
	// TU's compile time only — supplementary context, not the anchor.
	UE_LOG(LogCathodeBuild, Display, TEXT("[CATHODE-BUILD-SIGNATURE] Cathode plugin loaded; build-id %s (module-TU compiled %s %s)"),
		*Cathode::CathodeLinkTimeBuildId(), ANSI_TO_TCHAR(__DATE__), ANSI_TO_TCHAR(__TIME__));

	// Auto-start on module load (human-ruled): create the worker AND start its thread, which runs the
	// ordered Startup() sequence (create -> budgets -> assemble globals -> load author JS -> begin
	// ticking). The linked-native game-state object (game.*) attaches AT startup, not lazily — deferring
	// the worker would defer that attach and risk inconsistent state. A plugin included in the engine is
	// assumed to be in use, so there is no idle-spin-up to avoid.
	Worker = MakeUnique<Cathode::FCathodeRuntimeWorker>();
	Worker->Start();
}

void FCathodeRuntimeModule::ShutdownModule()
{
	TeardownWorker();
}

void FCathodeRuntimeModule::InstallSurfaceRelayoutSeam(Cathode::FSurfaceRelayoutSeam Seam)
{
	// 028 S2 — the only production-reachable path to the (Private) worker's Phase-5 seam slot. POST the install onto
	// the worker's inbound FIFO (applied on the WORKER thread, between ticks) — directly writing the seam from the
	// game thread would RACE the worker's tick read of it. The auto-started worker runs the seam each tick on its own
	// thread, so the composition runs on the worker.
	if (Worker.IsValid())
	{
		Cathode::FCathodeRuntimeWorker* const WorkerPtr = Worker.Get();
		WorkerPtr->PostInbound(Cathode::FHostInboundMessage{
			[WorkerPtr, Seam = MoveTemp(Seam)]() mutable { WorkerPtr->SetSurfaceRelayoutSeam(MoveTemp(Seam)); } });
	}
}

int32 FCathodeRuntimeModule::DrainOutbound(TArray<Cathode::FHostOutboundMessage>& Out)
{
	// Drain the worker's frame-ready outbound on the calling (game) thread — the host pumps the egress upload half per
	// frame-ready. No-op (0) if no worker is live.
	return Worker.IsValid() ? Worker->DrainOutbound(Out) : 0;
}

void FCathodeRuntimeModule::PostWorkerJob(TFunction<void()> Job)
{
	// 028 S5 — post a job onto the worker FIFO (run on the WORKER thread, between ticks) — the same PostInbound path the
	// seam-install uses. The owner posts the per-surface doc Dispose here at teardown (quickjs-affine). No-op if no worker.
	if (Worker.IsValid())
	{
		Worker->PostInbound(Cathode::FHostInboundMessage{ MoveTemp(Job) });
	}
}

void FCathodeRuntimeModule::TeardownWorker()
{
	// SINGLE-SOURCE teardown (shared by ShutdownModule + the test-only StopRuntimeForTesting): Worker::Shutdown
	// joins the worker thread and tears the runtime down ON that thread (affinity-correct), then
	// ~FQuickJsRuntime releases the single-instance claim. Idempotent: no-op if no worker is live.
	if (Worker.IsValid())
	{
		Worker->Shutdown();
		Worker.Reset();
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void FCathodeRuntimeModule::StopRuntimeForTesting()
{
	// Run the EXACT production teardown (the shared helper) — no test-specific path that could diverge.
	// After this the claim is free + no runtime is live (coherent), so an automation test can
	// CreateJsRuntime() its own instance on the TEST thread.
	TeardownWorker();
}

bool FCathodeRuntimeModule::IsWorkerRunningForTesting() const
{
	return Worker.IsValid();
}

void FCathodeRuntimeModule::RestartWorkerForTesting()
{
	// Restore the auto-started worker IF it was released (by a prior FScopedTestRuntime test). The single-instance
	// claim is FREE post-release, so re-claim + Start is the SAME path as the auto-start (StartupModule). RESTORE-IF-
	// RELEASED — no double-start when the worker is already live (e.g. the gate run in isolation).
	if (!Worker.IsValid())
	{
		Worker = MakeUnique<Cathode::FCathodeRuntimeWorker>();
		Worker->Start();
	}
}
#endif

IMPLEMENT_MODULE(FCathodeRuntimeModule, CathodeRuntime)
