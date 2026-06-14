// Cathode runtime-verification faculty (sprint 011 [b]) — runs IN a Cathode runtime and emits a structured,
// machine-readable self-report.
//
// EMIT: the faculty calls globalThis.__cathodeEmitReport(jsonString), a NATIVE sink CathodeVerify installs on
// the runtime before evaluating this. (Not console — the bare runtime has no console, itself a faculty-A
// finding; and ICathodeJsRuntime::Evaluate returns no completion value [FEvalResult = {bThrew, Error}], so the
// eval-result path can't carry the report. The native sink is the capture path that works on a bare runtime.)
//
// Driven by CathodeVerify's C++, which first injects the shared walk (surface-walk.js -> globalThis.
// CathodeSurfaceWalk), the link-id (globalThis.__CATHODE_BUILD_ID__), and the declared CSS subset
// (globalThis.__CATHODE_CSS_SUBSET__), then Evaluates this faculty.
//
// Faculty-A enumerates the ACTUALLY-exposed surface (reflection over the real runtime — NO hardcoded list).
// All three declared axes read DORMANT-until-baseline: DOM/globals' declared source is the not-yet-emitted
// 012 descriptor; CSS's is Cathode's OWN litehtml-grounded CSS declaration (a roadmap item in the plugin's
// Authoring/, never read from panels/). Each reports the dormant slot until its source lands. The DOM/globals
// reconcile MACHINERY is
// BUILT and consumes the declared-interface DESCRIPTOR (globalThis.__CATHODE_DECLARED__) the 012 Host Bridge
// projection emits — the descriptor shape is the durable 011<->012 lockstep contract pinned in
// CathodeVerify/SCHEMA.md (§2). It is DORMANT in 011 (no descriptor source yet) — it reports the dormant
// slot, never a re-walk-as-declared, and activates with no reshape when 012 injects the descriptor.
// Faculty-B is the capability framework (scaffold + a self-test; the 4 come-due proofs are [c]'s).
(function () {
  "use strict";

  // --- Harness-injection exclusion (review's core [b] gate) ---
  // The CathodeVerify driver injects scaffolding onto the test runtime to DRIVE + CAPTURE the faculty. These
  // names are NOT part of the production surface (production exposes none of them), so faculty-A MUST exclude
  // them from the enumerated surface — else it reports a false OVER-exposure that diverges from production by
  // the harness's own additions. Each is documented harness-not-surface; the C++ driver additionally ASSERTS
  // that the runtime's globals-additions == exactly this set, so the list cannot silently drift.
  var HARNESS_INJECTIONS = {
    "__cathodeEmitReport": "native emit-sink the driver installs to capture this report (capture channel, not surface)",
    "__CATHODE_BUILD_ID__": "cross-module aggregate link-id the driver injects as the coherence anchor (harness input, not surface)",
    "__CATHODE_CSS_SUBSET__": "declared CSS contract the driver injects for the CSS reconcile (harness input, not surface)",
    "CathodeSurfaceWalk": "the shared walk the driver injects (the introspection mechanism, not surface-under-test)",
    "__CATHODE_DECLARED__": "the declared-interface descriptor the 012 Host Bridge projection injects for the DOM/globals reconcile (faculty input, not surface; dormant until 012)",
  };

  // --- Faculty-A: enumerate the ACTUALLY-exposed surface (reflect the real runtime; no hardcoded list),
  //     filtering out the harness injections so the reported set == the PRODUCTION surface. ---
  function enumerateGlobals() {
    var all;
    try { all = Object.getOwnPropertyNames(globalThis); } catch (e) { return []; }
    var out = [];
    for (var i = 0; i < all.length; i++) {
      if (!Object.prototype.hasOwnProperty.call(HARNESS_INJECTIONS, all[i])) out.push(all[i]);
    }
    return out.sort();
  }
  function isExposed(name) { try { return typeof globalThis[name] !== "undefined"; } catch (e) { return false; } }

  // Contract host-globals (spec-surface axis 1) we PROBE FOR PRESENCE on the real runtime — a presence probe
  // of what's actually exposed, NOT a declared source (the declared SET is interface-extraction-derived, below).
  var HOST_GLOBALS = ["console", "setTimeout", "clearTimeout", "requestAnimationFrame", "document", "getComputedStyle"];

  function exposedSurface() {
    var hostGlobals = {};
    for (var i = 0; i < HOST_GLOBALS.length; i++) hostGlobals[HOST_GLOBALS[i]] = isExposed(HOST_GLOBALS[i]);
    var walk = (typeof globalThis.CathodeSurfaceWalk === "function") ? globalThis.CathodeSurfaceWalk() : null;
    return { globalNames: enumerateGlobals(), hostGlobals: hostGlobals, domSnapshot: walk };
  }

  // --- Faculty-A: CSS-axis reconcile — DORMANT in [b] (browse-pm/lead ruled 2026-06-14), parallel to
  //     DOM/globals. Its declared source is CATHODE'S OWN litehtml-grounded CSS declaration — a roadmap item
  //     authored later in the plugin's Authoring/ (the convention track), NEVER read/copied from panels/
  //     (another dev's area, untouched). With no Cathode CSS declaration injected, the reconcile reports the
  //     dormant slot. The CSS reconcile machinery proper is authored WITH that declaration (its shape lands
  //     with it), so [b] carries no panels-derived CSS shape. ---
  function cssReconcile(exposed) {
    return {
      declared: null,
      declaredSource: "Cathode's own litehtml-grounded CSS declaration (Authoring/, roadmap) -- pending (dormant)",
      drift: null,
      note: "CSS reconcile dormant: declared source is Cathode's own litehtml-grounded CSS declaration (pending, authored on the convention/roadmap track); never reads panels/",
    };
  }

  // --- DOM + globals reconcile MACHINERY (the 011<->012 lockstep). Consumes the declared-interface
  //     DESCRIPTOR (globalThis.__CATHODE_DECLARED__) emitted by the 012 Host Bridge single-source
  //     projection. The descriptor SHAPE is the durable contract pinned in CathodeVerify/SCHEMA.md (§2).
  //     The set-diff is baseline-AGNOSTIC (works over whatever declared-set is supplied). DORMANT in 011
  //     (no descriptor injected) -> reports the dormant slot, NEVER a re-walk-as-declared; 012 activates
  //     it with no reshape. ---

  // missing = declared keys NOT exposed (under-exposure); extra = exposed names NOT declared (over-exposure).
  // Only the key participates; tag/signature on declared entries are carried metadata, not diffed.
  function reconcileAxis(declaredEntries, keyFn, exposedNames) {
    var declaredKeys = {}, i;
    for (i = 0; i < declaredEntries.length; i++) declaredKeys[keyFn(declaredEntries[i])] = true;
    var exposedKeys = {};
    for (i = 0; i < exposedNames.length; i++) exposedKeys[exposedNames[i]] = true;
    var missing = [], extra = [], k;
    for (k in declaredKeys) { if (declaredKeys.hasOwnProperty(k) && !exposedKeys[k]) missing.push(k); }
    for (i = 0; i < exposedNames.length; i++) { if (!declaredKeys.hasOwnProperty(exposedNames[i])) extra.push(exposedNames[i]); }
    return { declaredCount: declaredEntries.length, exposedCount: exposedNames.length, missing: missing.sort(), extra: extra.sort() };
  }

  // axis: "dom" | "globals"; exposedNames: the actually-exposed set for that axis, keyed to match keyFn.
  var DESCRIPTOR_REF = "__CATHODE_DECLARED__ (012 Host Bridge single-source projection); shape: CathodeVerify/SCHEMA.md";
  function reconcile(axis, exposedNames) {
    var descriptor = (typeof globalThis.__CATHODE_DECLARED__ !== "undefined") ? globalThis.__CATHODE_DECLARED__ : null;
    var declared = (descriptor && descriptor[axis]) ? descriptor[axis] : null;
    if (!declared) {
      return { // DORMANT: no declared source in 011; machinery built + ready, activates on the 012 descriptor
        declared: null,
        declaredSource: DESCRIPTOR_REF + " -- not injected (dormant)",
        drift: null,
        note: axis + " declared set is DEFINITION-derived from the 012 projection (interface-IS-source), never a re-walk; reconcile machinery built + dormant pending the descriptor",
      };
    }
    var keyFn = (axis === "dom")
      ? function (e) { return (e.iface || "") + "." + e.name; } // DOM: same member name across interfaces
      : function (e) { return e.name; };                        // globals: name
    return { declaredSource: DESCRIPTOR_REF, declaredCount: declared.length, drift: reconcileAxis(declared, keyFn, exposedNames) };
  }

  // Exposed sides the reconcile diffs the declared set against (see SCHEMA.md §2 "Exposed side"):
  // - globals: the exposed host-globals (probe trues) only — NOT ECMA-262 built-ins (those are axis-1 baseline).
  // - DOM: the members reachable off the real DOM wrapper; empty on a bare runtime (no document).
  function presentHostGlobals(exposed) {
    var out = [], name;
    for (name in exposed.hostGlobals) { if (exposed.hostGlobals.hasOwnProperty(name) && exposed.hostGlobals[name]) out.push(name); }
    return out;
  }
  function exposedDomMembers(exposed) {
    return []; // bare runtime: no DOM wrapper. When wired, enumerate its members keyed iface.name to match.
  }

  // --- Faculty-B: exercise-and-report FRAMEWORK (scaffold). [c] registers the 4 come-due proofs. ---
  function makeCapabilityFramework() {
    var probes = [];
    return {
      register: function (name, exercise) { probes.push({ name: name, exercise: exercise }); },
      run: function () {
        var out = [];
        for (var i = 0; i < probes.length; i++) {
          var p = probes[i], r = { name: p.name, exercised: false, ok: null, realOutput: null, error: null };
          try { var v = p.exercise(); r.exercised = true; r.ok = !!(v && v.ok); r.realOutput = (v && v.value !== undefined) ? v.value : null; }
          catch (e) { r.error = String(e); }
          out.push(r);
        }
        return out;
      },
    };
  }

  // --- assemble ---
  var exposed = exposedSurface();
  var framework = makeCapabilityFramework();
  framework.register("framework-self-test", function () { return { ok: true, value: "capability framework ran" }; });
  var capabilities = framework.run();

  var report = {
    schema: "cathode.runtime-verification/1",
    coherence: { buildId: (typeof globalThis.__CATHODE_BUILD_ID__ !== "undefined") ? globalThis.__CATHODE_BUILD_ID__ : "unavailable" },
    surface: {
      exposed: {
        globalNames: exposed.globalNames,         // production globals (harness injections filtered out)
        hostGlobals: exposed.hostGlobals,         // which contract host-globals are actually exposed (bare: ~none)
        domExposed: exposed.domSnapshot !== null, // false on a bare runtime (no document) — a faculty-A finding
        harnessExcluded: Object.keys(HARNESS_INJECTIONS), // auditable: the harness names filtered from globalNames
      },
      dom: reconcile("dom", exposedDomMembers(exposed)),       // dormant in 011 (no descriptor); bare DOM == []
      globals: reconcile("globals", presentHostGlobals(exposed)), // dormant in 011; exposed host-globals only
      css: cssReconcile(exposed),
    },
    capabilities: capabilities,
    snapshot: exposed.domSnapshot,
  };

  var json = JSON.stringify(report);
  if (typeof globalThis.__cathodeEmitReport === "function") {
    globalThis.__cathodeEmitReport(json);
  } else if (typeof console !== "undefined" && console && typeof console.log === "function") {
    console.log(json); // fallback (e.g. observation-style host with console) — not the bare-runtime path
  }
  return json; // also the completion value, harmless; the native sink is the capture path
})();
