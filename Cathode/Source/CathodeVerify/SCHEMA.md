# CathodeVerify schemas

Two machine-readable schemas the runtime-verification faculty (sprint 011 [b]) settles. Both live
**on-disk in the module** so they are durable + referenceable across sprints (not coordination-only):

1. **The self-report** the faculty emits — what a verification run produces.
2. **The declared-interface descriptor** the faculty's DOM/globals reconcile *consumes* — the
   **011↔012 lockstep contract**: 011 builds the consumer (the reconcile machinery) and pins the
   shape here; the 012 Host Bridge single-source projection is the *producer* and MUST emit exactly
   this shape, so the dormant reconcile activates without a reshape.

The authoritative implementations are `JS/runtime-verify.js` (faculty + reconcile machinery) and
`JS/surface-walk.js` (the shared snapshot walk). This file is the durable contract of record.

---

## 1. Self-report — `cathode.runtime-verification/1`

The faculty emits one JSON object via the native sink `globalThis.__cathodeEmitReport(jsonString)`
(the bare runtime has no console, and `ICathodeJsRuntime::Evaluate` carries no completion value, so
the native sink is the capture path). Shape:

```
{
  schema: "cathode.runtime-verification/1",
  coherence: { buildId: string },          // cross-module aggregate link-id — every loaded Cathode module's PE
                                           //   link-id folded LIVE; advances on ANY Cathode-module relink
  surface: {
    exposed: {
      globalNames:     string[],           // production globals, harness injections filtered out (see below)
      hostGlobals:     { [name]: boolean }, // presence probe of the contract host-globals
      domExposed:      boolean,             // false on a bare runtime (no document) — a faculty-A finding
      harnessExcluded: string[],           // the harness names filtered from globalNames (auditable)
    },
    dom:     AxisReconcile,                 // see §2 — dormant-until-baseline (012 descriptor)
    globals: AxisReconcile,                 // see §2 — dormant-until-baseline (012 descriptor)
    css:     AxisReconcile,                 // see §3 — DORMANT in [b]; declared source = Cathode's OWN
                                            //   litehtml-grounded CSS declaration (Authoring/, roadmap), never
                                            //   panels/. Parallel to dom/globals.
  },
  capabilities: CapabilityResult[],         // faculty-B: exercised real primitives ([c] registers the 4)
  snapshot: SurfaceSnapshot | null,         // the shared walk's tree (null on a bare runtime)
}
```

`harnessExcluded` is the set of driver-injected names faculty-A removes from `globalNames` so the
reported surface == the **production** surface, not the harness-augmented runtime. The C++ driver
additionally asserts the runtime's globals-additions == exactly this set, so it cannot silently drift.

---

## 2. Declared-interface descriptor — the 011↔012 lockstep contract

faculty-A's DOM/globals reconcile consumes a **descriptor**: the declared-interface set, per axis,
injected as `globalThis.__CATHODE_DECLARED__` by the 012 Host Bridge single-source projection
(interface-IS-source — generated from the engine's own binding interfaces, never a hand-list, never a
re-walk-as-declared). 011 pins the shape; 012 emits to it.

```
__CATHODE_DECLARED__ = {
  dom:     DeclaredDomMember[],
  globals: DeclaredGlobal[],
}

DeclaredDomMember = {
  iface:      string,                       // owning interface, e.g. "Node" | "Element" | "Document"
  name:       string,                       // JS member name, e.g. "appendChild" | "childNodes" | "textContent"
  kind:       "method" | "property",
  tag?:       "required" | "margin" | "reflected" | "obs",  // contract class (spec-surface); carried metadata
  signature?: string,                       // optional, informational (e.g. "(child: Node) -> Node"); not diffed
}

DeclaredGlobal = {
  name:       string,                       // global name, e.g. "console" | "setTimeout" | "requestAnimationFrame"
  kind:       "function" | "object",
  signature?: string,                       // optional, informational; not diffed
}
```

### Reconcile key + diff (baseline-agnostic — works over whatever declared-set is supplied)

- **DOM** entries key on `iface + "." + name` (the same member name can exist on multiple interfaces).
- **globals** entries key on `name`.
- The diff is set-membership only:
  - `missing` = declared keys **not** in the exposed set → **under-exposure** (declared, not built).
  - `extra`   = exposed names **not** in the declared set → **over-exposure** (built, not declared).
- Only `iface`/`name`/`kind` form the key; `tag`/`signature` are carried metadata, not diffed.

### Exposed side (what `missing`/`extra` compare the declared set against)

- **DOM**: the DOM members faculty-A enumerates off the real DOM wrapper. Empty on a bare runtime
  (no document) — so an injected DOM descriptor would surface as all-`missing` (the under-exposure finding).
- **globals**: the **exposed host-globals** (the presence-probe trues) — **not** the full ECMA-262
  built-in set (`Object`/`Array`/… are the JS baseline = spec-surface axis 1, not provided host-globals;
  including them would be a false over-exposure).

### `AxisReconcile` shape (what §1's `surface.dom` / `surface.globals` hold)

```
// Dormant (011 — no descriptor injected):
{ declared: null, declaredSource: string, drift: null, note: string }

// Active (012 — __CATHODE_DECLARED__ injected):
{ declaredSource: string, declaredCount: number,
  drift: { declaredCount, exposedCount, missing: string[], extra: string[] } }
```

### Dormancy in 011

No descriptor exists in 011 (no source until 012). The reconcile machinery is **built + ready** but
reports the dormant slot (`declared:null`, `drift:null`) and never fabricates a declared set. 012
injecting `__CATHODE_DECLARED__` activates it with no reshape. `__CATHODE_DECLARED__` is listed in the
faculty's `HARNESS_INJECTIONS` exclusion (it is a faculty *input*, not production surface), so when 012
wires it, it does not pollute the enumerated surface.

---

## 3. CSS reconcile (DORMANT in [b], parallel to DOM/globals)

The CSS axis is **dormant** in [b], the same `AxisReconcile` dormant shape as DOM/globals. Its declared
source is **Cathode's OWN litehtml-grounded CSS declaration** — a roadmap item authored later in the
plugin's `Authoring/` (the self-containment convention), grounded in what litehtml *actually* supports
(more correct than mirroring another tool's approximation).

**Never panels/.** `panels/css-subset.json` is another developer's area (panels-managed) and is **left
untouched** — Cathode does not read, copy, sync, or re-home it. Internalizing means *authoring our own
conformant declaration from the intention*, not importing panels' file. (An earlier [b] iteration read
panels/ at runtime then at build; both were reverted — the build box carries the UE project only, and
more fundamentally the cross-area read violates the internalize convention.)

```
// Dormant (CSS in [b] — Cathode's own declaration not yet authored):
{ declared: null, declaredSource: string /* "Cathode's own ... (Authoring/, roadmap) -- pending (dormant)" */,
  drift: null, note: string }
```

**When the declaration lands** (roadmap, browse-pm sequences it): the CSS reconcile machinery is authored
*with* it — its declared-set shape and the honored-vs-declared probe (vs the CSSOM, at integration) land
together. [b] deliberately carries **no** CSS declared-set shape, so nothing here prejudges that design.

**Roadmap = AUTHOR (litehtml-grounded), not fabricate:** the declaration is authored against litehtml's
real CSS coverage when directed; the faculty never guesses coverage and never imports panels' list.
