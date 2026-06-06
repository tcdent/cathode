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
// globals, and it does NOT bind a JS global here: actually binding requires the JS<->Native Bridge plus
// a live context (neither exists this sprint). This unit only sequences the registration points and
// hands each seam its real target where one already exists.
//
// Targets that already exist are delivered to their seams: the console seam receives the Slice-5
// diagnostic sink; the timer/rAF seam receives the Slice-4 scheduler. The game.* (Host Bridge) and
// document/mount (DOM) seams are deliberately EMPTY now — their owning components are future sprints.
class FGlobalEnvironment
{
public:
	// Registration seams, set by the owning components (future). A set seam is invoked, in order, during
	// assembly; an unset seam is the deliberate not-yet-wired placeholder.
	TFunction<void(ICathodeDiagnosticSink&)> OnInstallConsole; // wired target: the diagnostic sink
	TFunction<void(FCathodeScheduler&)> OnInstallTimers;       // wired target: the scheduler
	TFunction<void()> OnInstallGameBridge;                     // empty: Host Bridge (game.*), future
	TFunction<void()> OnInstallDocument;                       // empty: DOM document/mount root, future

	// Assemble the globals in contract order, handing each seam its real target. Called by the runtime
	// startup sequence; the specs call it directly with recording seams to assert the order.
	void AssembleGlobals(ICathodeDiagnosticSink& Sink, FCathodeScheduler& Scheduler)
	{
		if (OnInstallConsole)
		{
			OnInstallConsole(Sink); // console.* routes into the diagnostic sink
		}
		if (OnInstallTimers)
		{
			OnInstallTimers(Scheduler); // setTimeout / requestAnimationFrame wire onto the scheduler
		}
		if (OnInstallGameBridge)
		{
			OnInstallGameBridge(); // game.* — Host Bridge component, future
		}
		if (OnInstallDocument)
		{
			OnInstallDocument(); // document / mount root — DOM component, future
		}
	}
};

} // namespace Cathode
