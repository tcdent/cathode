#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Cathode runtime-verification module (CathodeVerify).
 *
 * The in-engine home of the runtime-verification faculty (sprint 011 [b]):
 *   (A) surface introspection — enumerate the surface the engine ACTUALLY exposes at runtime and reconcile
 *       it against the declared contract (surface-drift visible);
 *   (B) the capability-verification framework — exercise the real primitives and report whether they work
 *       (the framework here; the 4 come-due proofs are [c]'s to register).
 * Output: a structured, machine-readable self-report. Folds in observe/observer.js's surface-walk as the
 * SHARED introspection mechanism — the same walk observation.md's external-parity observer uses, different
 * purpose (internal self-report vs external Chromium parity); the walk is shared, not duplicated.
 *
 * A passive capability: the faculty is driven on demand (in a Cathode runtime) and emits its self-report;
 * nothing auto-runs at module load. See docs/browse/components-runtime-verification.md.
 */
class FCathodeVerifyModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
