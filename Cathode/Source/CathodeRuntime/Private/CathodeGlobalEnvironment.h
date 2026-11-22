#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace Cathode
{

class ICathodeDiagnosticSink;
class FCathodeScheduler;

// Global-environment assembly orchestration. Runtime Core SEQUENCES the ordered installation of the
// contract globals — console, the timer/rAF surface, the game.* bridge object, the document/mount root
// — each via a REGISTRATION SEAM the owning component plugs into. Runtime Core does NOT implement the
// globals, and it does NOT bind a JS global here: the seam closures do the binding on the live context
// (AssembleContractGlobals sets the console/timers ones). This unit only sequences the registration points
// and hands each seam its real target.
//
// The console and rejection-tracker seams receive the diagnostic sink; the timer/rAF seam receives the
// scheduler. The game.* (Host Bridge) and document/mount (DOM) seams are not set by Runtime Core — an unset
// seam is skipped (the document is installed per surface via FDomDocument::InstallAsGlobal).
class FGlobalEnvironment
{
public:
	// Registration seams, set by the owning components. A set seam is invoked, in order, during
	// assembly; an unset seam is skipped.
	TFunction<void(ICathodeDiagnosticSink&)> OnInstallConsole; // wired target: the diagnostic sink
	TFunction<void(ICathodeDiagnosticSink&)> OnInstallRejectionTracker; // installs JS_SetHostPromiseRejectionTracker -> the sink
	TFunction<void(FCathodeScheduler&)> OnInstallTimers;       // wired target: the scheduler
	TFunction<void()> OnInstallGameBridge;                     // Host Bridge (game.*); unset by Runtime Core
	TFunction<void()> OnInstallDocument;                       // DOM document/mount root; unset by Runtime Core

	// Assemble the globals in contract order, handing each seam its real target. Called by the runtime
	// startup sequence; the specs call it directly with recording seams to assert the order.
	void AssembleGlobals(ICathodeDiagnosticSink& Sink, FCathodeScheduler& Scheduler)
	{
		if (OnInstallConsole)
		{
			OnInstallConsole(Sink); // console.* routes into the diagnostic sink
		}
		if (OnInstallRejectionTracker)
		{
			OnInstallRejectionTracker(Sink); // the host promise-rejection tracker routes unhandled rejections into the sink
		}
		if (OnInstallTimers)
		{
			OnInstallTimers(Scheduler); // setTimeout / requestAnimationFrame wire onto the scheduler
		}
		if (OnInstallGameBridge)
		{
			OnInstallGameBridge(); // game.* — Host Bridge component
		}
		if (OnInstallDocument)
		{
			OnInstallDocument(); // document / mount root — DOM component
		}
	}
};

} // namespace Cathode
