# Cathode — Systems & Components

This is the high-level component map: the modules Cathode is made of, what each is responsible
for, and why it exists. It does **not** specify how any of them work internally — each component
gets its own drill-down doc, and the implementing team owns the internal design. References to
specific libraries/APIs are grounding, not design.

See `architecture.md` for the system model these components realize.

## How the pieces fit (data flow)

```
host game ──state/events──▶ [Runtime Core] ──drives──▶ author JS
                                  │                        │ mutates
                                  │                        ▼
                          [JS↔Native Bridge] ◀────────▶ [DOM]
                                                           │ dirties
                                                           ▼
                                              [Layout & Style] ◀─measure─ [Text & Fonts]
                                                           │ draw calls       │ glyph runs
                                                           ▼                  ▼
                                                      [Rasterizer] ───────────┘
                                                           │ CPU bitmap (dirty rect)
                                                           ▼
                                       [Surfaces] ──"frame ready"──▶ [Texture Egress] ──▶ UTexture2D

   side channels: [Resource Resolver] feeds Layout/JS · [Host Bridge `game.*`] is the host API ·
                  [Input Routing] turns world input into DOM events · [Contract & Gate] +
                  [Plugin Shell] are build/author-side.
```

## Components

Grouped into layers for the big picture; each leaf is a candidate drill-down doc.

### Layer 1 — Host & runtime foundation

**Runtime Core.** Owns the single quickjs-ng runtime, the dedicated worker thread, and the tick —
the microtask/job pump plus timers and `requestAnimationFrame` on the worker's own clock — and
runtime startup/teardown. *Why:* there must be exactly one owner of the shared runtime, the
thread, and the frame loop; the shared-global, many-surfaces model depends on a single runtime
instance off the game's critical path. *Ground:* quickjs-ng is one-runtime-per-thread; the host
drives the job queue and the timer/RAF clock.

**JS↔Native Bridge.** The machinery that exposes C++ objects and functions to JS and manages
object lifetime across the two heaps. *Why:* everything native that JS touches (DOM nodes, the
host API) flows through here, and the cross-heap reference cycle is the system's single biggest
correctness hazard — it needs one disciplined owner. *Ground:* quickjs-ng's native
class/opaque/finalizer API with RAII over refcounts; finalization-driven cleanup.

### Layer 2 — The web engine (DOM → pixels)

**DOM.** The retained node tree and the bound DOM API surface authors program against (the
documented subset), including the `Event` object and dispatch. *Why:* it's what author JS
manipulates and the input to layout; keeping the surface bounded is the firewall against
accidentally rebuilding a browser. *Ground:* the contract's DOM axis.

**Layout & Style.** Feeds the DOM tree into litehtml, drives relayout when the tree is dirtied,
and routes paint through litehtml's container callbacks. *Why:* turns DOM +
CSS into positioned boxes and draw calls and defines the CSS coverage authors get. *Ground:*
litehtml `document_container`; litehtml is parse-then-render, so this component owns the
mutation→relayout adaptation.

**Text & Fonts.** Shapes and measures text with HarfBuzz, manages fonts and shape-plan caching,
and feeds both measurement (which layout depends on) and positioned glyph runs (which the
rasterizer draws). *Why:* text fidelity is a first-class goal, and correct measurement is a hard
layout dependency. *Ground:* HarfBuzz GSUB/GPOS with per-run feature control; rendering is
OS-independent by design.

**Rasterizer.** Implements the paint backend that turns litehtml's draw calls into pixels in a CPU
bitmap, rasterizing only dirty regions, on the worker thread. *Why:* it produces the actual frame
off the game's render path. *Ground:* Blend2D (JIT-backed via AsmJit); draws boxes/images plus
HarfBuzz glyph runs.

### Layer 3 — Surfaces & output

**Surfaces.** The per-surface abstraction — a document plus its output buffer(s) and dirty-rect
tracking — and the mapping of one shared runtime onto many independent surfaces, from wall
dashboards down to single-character LCDs. *Why:* the many-surfaces model and "what is ready to
upload" live here. *Ground:* each surface is one litehtml document; buffering is double/triple.

**Texture Egress.** The render-thread side: on "frame ready," uploads the dirty sub-rectangle into
the surface's texture and owns those GPU texture resources. *Why:* the single, RHI-agnostic
touchpoint that gets pixels onto a material, firing only on change. *Ground:* regional
`RHIUpdateTexture2D`; render-thread affinity.

### Layer 4 — Integration with the game and world

**Resource Resolver.** Resolves URLs to bundled content for litehtml resource requests and JS
module loading. *Why:* the mounted-assets model (no HTTP server) needs one place that maps logical
URLs to bundled files. *Ground:* litehtml resource callbacks + the quickjs module loader; author
JS is pre-bundled.

**Host Bridge (`game.*`).** The curated public host API exposed to JS — state ingress with change
signals, action egress via host function calls — generated from Unreal reflection. *Why:* it's how
screens read and affect the game, and one definition must drive the runtime bindings, the author
types, and the build gate so they can't drift. *Ground:* UE reflection → quickjs bindings +
`lib.cathode.d.ts`; the API's code organization is intentionally deferred until the sim exists.

**Input Routing.** Routes keyboard input (and, later, the controller's semantic events) to the
active surface and manages focus within it, dispatching DOM events. *Why:* it makes screens
interactive by getting input to the right document and element. *Ground:* `keydown` on `document`,
`element.focus()` / `:focus`. (Pointer/hit-test input is deferred — see
`components-input-routing.md`.)

### Layer 5 — Authoring & build

**Authoring Contract & Gate.** The tooling that defines and enforces the three-axis subset:
`lib.cathode.d.ts`, the CSS manifest and linter, the capability manifest as single source of
truth, the build-time hard-fail, plus the runtime backstop and developer overlay. *Why:* it keeps
authored screens inside what the engine supports and makes them debuggable; it is build/author-side,
distinct from the runtime. *Ground:* the TypeScript compiler is the DOM/JS gate; the manifest is
asserted against the registered bindings at engine build.

**Runtime Verification.** A built-in capability that, at runtime, introspects the surface the engine
*actually* exposes (reconciled against the declared contract) and verifies the real primitives
*actually* function — emitting a structured, machine-readable self-report. *Why:* the build gate is
static (author code ⊆ declared surface); this is its runtime counterpart (declared == actually-exposed-
and-working), and the machine-perception surface for the AI dev loop. Folds in the `observe/` harness
(browse-owned) as its in-engine home. *Ground:* the `observe/observer.js` introspect-and-emit lineage +
the capability-probe pattern (obs-browser/UE-Uranium); see `components-runtime-verification.md`.

**Plugin Shell & Build.** The Unreal plugin packaging: the `.uplugin`, `Build.cs`, ThirdParty
vendoring of the four libraries (plus AsmJit alongside Blend2D), module wiring, and project
enablement. *Why:* it makes Cathode a buildable, reusable Unreal plugin following the repo's
native-library conventions. *Ground:* the JoystickPlugin/SDL2 vendoring pattern; builds run on the
Windows box over SSH.

## Notes

- The layering is for understanding, not a strict dependency order. The drill-down sequence is the
  team's to decide.
- Some leaves may merge in practice (e.g., Layout/Text/Rasterizer form one rendering pipeline;
  Runtime Core may absorb the threading/messaging boundary). Treat the list as the set of
  responsibilities to cover, not a mandated module count.
