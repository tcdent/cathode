# Cathode — Architecture

Cathode (internal codename *Browse*) is an **Unreal Engine 5.7 C++ plugin** that runs standard
HTML/CSS/JS "screens" and renders them as textures onto in-world devices — rack KVMs, crash-cart
laptops, wall dashboards, even single-character device LCDs. It is assembled from small open
libraries; Cathode authors no HTML engine, JavaScript runtime, or CSS parser of its own.

This document defines the system's design and the technologies it is built on. These are
decisions, not options. Sprint ordering and execution are the implementing team's to own.

## Purpose and scope

Cathode exists to let screens in the world be authored as ordinary, interactive web pages, and to
get their pixels onto device surfaces efficiently. It is a **rendering surface for first-party,
authored screens** — not a web browser.

**In scope:** a JavaScript runtime, a constrained DOM, HTML/CSS layout, text shaping and CPU
rasterization, the upload of rendered frames to Unreal textures, and a typed bridge between
screens and the host game.

**Out of scope:** networking, TLS, cookies, client-side storage, history/navigation chrome,
multi-tab, extensions — the entire "browser" apparatus. Screen content is bundled and trusted, so
none of that machinery is needed, and excluding it is what keeps Cathode small.

Cathode is **explicitly an Unreal plugin** and leans on Unreal systems deliberately. It is **not**
coupled to UPTIME — it has no dependency on `Source/Uptime` — so it remains a reusable Unreal
plugin that any UE game can adopt.

## Why this design (the choices it rejects)

The technology choices below are the residue of eliminating everything that failed a hard
requirement. The requirements: **open and unencumbered** (a 20-year project horizon, forkable, no
subscription, no per-engine vendor gate), **small** (single-digit-MB runtime), **standard
HTML/CSS/JS with real interaction**, and **the engine's own render performance is irrelevant**
(screens are mostly static). What this rules out:

- **Embedding Chromium (CEF).** Open and standards-complete, but ~150 MB minimum — Chromium's
  expensive parts (Blink, V8, Skia, the compositor) are exactly the parts that can't be removed —
  plus a multi-process model and cross-process GPU texture sharing. Too heavy. A closed,
  per-UE-version precompiled CEF plugin additionally gates engine upgrades on a vendor and is
  disqualified outright.
- **Ultralight / Sciter.** Small and capable, but proprietary; Ultralight is subscription-priced.
  A recurring license at the core of a long-lived game is unacceptable.
- **WebKit ports.** WPE WebKit is offscreen-ideal but Linux-only; the Windows (WinCairo) port is
  HWND-bound with no clean GPU egress.
- **Servo.** Open and modern, but Rust, tens of MB, and pre-alpha to embed.

What satisfies every requirement is a **minimal runtime assembled from small open libraries** —
the design described here.

## The stack

Cathode is built from four libraries. These are the dependencies.

| Concern | Library | License | Role |
|---|---|---|---|
| JavaScript | **quickjs-ng** | MIT | the language only — runs author JS, native `Promise` |
| HTML + CSS | **litehtml** | BSD | parses + cascades + lays out + drives paint |
| Text shaping | **HarfBuzz** | MIT-family | shapes and measures text: kerning, ligatures, OpenType features |
| Rasterization | **Blend2D** | zlib | rasterizes shaped glyph runs, boxes, images into a CPU bitmap |

Plus a thin Cathode-authored layer: the DOM surface bridging quickjs-ng to litehtml's tree, the
worker/threading, the texture upload, and the host bridge.

Everything is C/C++, in-process, permissively licensed, built from source. No Chromium, no Rust,
no multi-process model, no cross-process shared textures. The runtime is single-digit megabytes.

## System model

### One shared runtime, many surfaces

There is **one quickjs-ng runtime with one global object** — the shared world-JS state. Each
physical display is a **surface**: one litehtml document with its own root element and its own
output bitmap/texture. A wall dashboard and a one-line device LCD are the same kind of object; the
LCD is simply a tiny document costing kilobytes.

Every surface reads from the **same shared global state**. This is the defining property: a button
pressed on one device can change a value that a dashboard across the room reacts to, with no
plumbing between them. The host pushes state into the shared global; surfaces re-render when the
state they read changes. Surfaces are not isolated from each other — this is intentional and is a
feature of the model, not a deficiency.

### Threading

The entire runtime — JS execution, DOM mutation, litehtml layout, HarfBuzz shaping, Blend2D
rasterization — runs on a **single dedicated worker thread**, single-threaded internally
(matching both the web's execution model and quickjs-ng's one-runtime-per-thread design). It is
**off the game's render and critical path**: surfaces re-lay-out at their own cadence and the
game's frame build never waits on them. `requestAnimationFrame` and timers tick on the worker's
own clock. The host and the runtime communicate over a **thread-safe message queue** — events and
state updates in, "frame ready" notifications out.

### Rendering and egress

Cathode **rasterizes on the CPU (worker thread) and uploads to the GPU (render thread).** This is
deliberate: GPU rasterization would require either pushing paint onto the render thread or
standing up a second GPU device on the worker and sharing textures across devices — reintroducing
exactly the cross-device shared-texture complexity that embedding Chromium would have forced.
Because screens are mostly static and the engine's render speed of the page does not matter, CPU
raster is strictly simpler with no meaningful cost.

The worker rasterizes into a double-buffered CPU bitmap. When content changes, the render thread
performs **one regional `RHIUpdateTexture2D` of the dirty sub-rectangle** into the surface's
`UTexture2D`. The upload is RHI-agnostic (no D3D/Vulkan/Metal-specific interop) and fires only on
change — idle surfaces cost nothing, a ticking digit uploads a few pixels.

### Asset delivery

Screen assets — HTML, CSS, JS, images, fonts — are **bundled and resolved through a virtual
resource resolver, not served over HTTP.** An internal HTTP server would mean parsing requests to
fetch files already held in memory. Neither library fetches on its own: litehtml asks Cathode to
resolve resources through its `document_container` callbacks, and quickjs-ng module loading is
host-provided. Authors write ordinary relative URLs; Cathode resolves them as logical paths into
bundled content. Author JavaScript is pre-bundled to a single file, so module resolution does not
arise at runtime.

### Data in and out

Screens reach the host through **direct host bindings, not a transport.** Screen JS runs in the
same process and runtime as the game's data, so the bridge is a function call across the C↔JS
boundary, not serialized request/response. State flows in by exposing host-backed values on a
global with change signals that trigger reactive re-render; actions flow out by the screen calling
host functions.

The in-app global is **`game`** — screens access the host game through `game.*`. The surface
authors call is a **curated public API**, never raw object access, so it stays a stable contract
and an encapsulation boundary. Because Cathode is an Unreal plugin, the binding layer is
**generated from Unreal reflection**: host functions marked for exposure produce both the
quickjs-ng bindings and the authoring type definitions from a single source, so the runtime
bindings, the author types, and the build gate cannot drift.

### Font rendering

Text fidelity is a first-class design goal — a 4K render target and a high bar for typography.
Cathode therefore **shapes with HarfBuzz and rasterizes with Blend2D**:

- **Fidelity:** HarfBuzz applies real GPOS kerning, GSUB ligatures, and OpenType features from the
  font's own tables — the difference between typeset text and naive advance-width layout,
  especially at 4K.
- **Control:** OpenType features are per-run toggles, so *deliberately* unkerned or raw text (retro
  CRT screens) is an aesthetic choice (`kern=0` on that run), not a limitation.
- **Independence:** because Cathode shapes and rasterizes itself, the operating system's text
  stack (ClearType, GDI hinting) never touches output — rendering is identical on every platform
  and Cathode chooses the hinting/AA philosophy (light hinting, grayscale AA).

HarfBuzz also provides the text measurement litehtml requires. Bidirectional (RTL) ordering is a
separate concern, not part of the baseline; it is added only if non-Latin RTL content becomes real.

### Frame lifecycle (per change)

1. The host posts an event or state update onto the worker queue.
2. The worker runs the JS handler, which mutates the retained DOM tree.
3. The mutation dirties the tree; litehtml re-lays-out (a small document, cheap) and reports dirty
   rectangles; text is shaped via HarfBuzz.
4. Blend2D rasterizes the dirty region; the buffer is swapped; a "frame ready" notification is
   posted.
5. The render thread uploads the dirty rectangle into the surface's `UTexture2D`.

## The authoring contract

A Cathode screen is a standard HTML/CSS/JS app **targeting a documented subset**. The contract has
three axes, and the build **hard-fails** on anything outside them — an unsupported feature is a
build error, not a runtime surprise.

1. **DOM** — a deliberately small surface: element creation/insertion/removal, attribute and
   property access, an `Event` with **bubble + capture** dispatch, tree reads, the scheduler
   (`Promise`, timers, `requestAnimationFrame`), and a document/mount root. The surface is
   intentionally bounded; it is the firewall against accidentally rebuilding a browser. (The
   authoritative member list is `components-dom-surface.md`.)
2. **CSS** — litehtml's coverage (CSS 2/3, flexbox; no grid; partial CSS3), extended only where
   Cathode forks litehtml for a specific need. This is where authoring constraints are concrete and
   documented.
3. **JS environment** — ECMA-262 core (quickjs-ng) at a pinned baseline, plus the globals Cathode
   provides (timers, `requestAnimationFrame`, console, the `game.*` API). Nothing else exists —
   no `fetch`, no `Intl`, no `localStorage`.

### The build-time gate

The gate is built from existing toolchains, not a bespoke analyzer:

- **DOM and JS API:** the **TypeScript compiler is the gate.** Cathode ships a `lib.cathode.d.ts`
  declaring only the supported surface; author apps compile against it without the standard DOM
  lib, so any unsupported API is a type error with a file and line. Authors get autocomplete and
  documentation for free. (TypeScript support is free — it compiles to plain JS the runtime never
  sees as types; plain JS type-checked against the same definitions is equally accepted.)
- **JS syntax level:** the bundler target is pinned to the runtime's ECMA baseline; newer syntax
  errors or down-levels.
- **CSS:** a small linter validates author CSS against a supported-property manifest.
- **Single source of truth:** one capability manifest generates the type definitions and the CSS
  allow-list and is asserted at engine build against the actually-registered bindings and litehtml
  configuration — so the gate provably matches the engine.
- **Runtime backstop:** dynamic access the static gate cannot see throws a clear, sourced error
  rather than silently returning undefined, surfaced to a developer overlay.

## The JavaScript runtime (quickjs-ng)

quickjs-ng is the chosen engine: maintained, MIT, ES2023-complete, with native `Promise`,
`WeakRef`/`FinalizationRegistry`, Proxy, BigInt, and typed arrays — the most complete small
engine. Its design implications shape Cathode:

- **It is only the language.** Every Web API is provided by Cathode or does not exist. This is the
  intent — Cathode controls the exact surface — and it is enforced by the contract.
- **JS↔C++ object lifetime is the central correctness concern.** DOM nodes are C++ objects with JS
  wrappers; a node holding a listener that captures the node is a cycle spanning both heaps that
  the collector cannot break alone. The mechanism is solved and standard (the native
  class/opaque/finalizer API, with RAII wrappers over the refcount); the policy — finalizers,
  explicit teardown on node and listener removal, finalization-driven C++ cleanup — is Cathode's
  to own.
- **The host owns the event loop.** Cathode pumps the microtask/job queue and provides
  timers/`requestAnimationFrame` against the worker clock.
- **Safety is deny-by-default.** The engine has no ambient authority; only the host functions
  Cathode exposes exist. Runaway scripts are bounded by an interrupt budget and per-runtime memory
  limits.
- **No JIT** — interpreted, slower than V8, and irrelevant here: UI logic on an off-thread runtime,
  with heavy per-frame JS excluded by the contract.

Missing batteries are handled by documentation and small shims: there is no `Intl`/ICU (locale
formatting is a separate spec requiring megabytes of data) and limited `Date` timezone support;
plain numeric and ISO formatting are core language and work.

## Plugin shape

Cathode is a standard Unreal plugin under `game/Plugins/Cathode/`, with its runtime module and a
`Source/ThirdParty/` housing the four libraries, following the project's established native-library
convention (headers plus platform libraries, `Build.cs` declaring include paths, additional
libraries, runtime dependencies, and delay-load where needed; binaries via Git LFS). Authoring
tooling — the type definitions, CSS manifest, capability manifest, and linter — lives alongside the
plugin. The runtime module depends only on Unreal modules (RHI, RenderCore, Slate) and the bundled
libraries.

## Constraints and known hard parts

These are properties of the design to build with eyes open, not open questions:

- **litehtml is parse-then-render**, not a live-mutation engine; Cathode drives relayout on dirty
  state and keeps its own retained tree as the source of truth, likely with a small litehtml fork
  for efficient mutation.
- **CSS coverage is litehtml's** — no grid, partial CSS3. Authoring constraints are real and
  documented; missing features are forked in only where actually needed.
- **JS↔C++ lifetime** is the area most prone to subtle bugs (see runtime notes).
- **Authoring developer experience** — the gate, error surfacing, and a developer overlay — is what
  makes screens pleasant to write; it is part of the system, not an afterthought.
- **Font/text rendering** is a real subsystem (HarfBuzz shaping feeding Blend2D rasterization), not
  a free dependency.

## Intentionally unspecified

One part of the design is deliberately left open because it depends on a system that does not yet
exist: the **organization and exact shape of the `game.*` API.** Settled regardless of how it
lands — it is a curated public API generated from Unreal reflection so bindings, author types, and
the gate stay in lock-step. Undecided — whether the host API is exposed from a single wrapper or
each game subsystem publishes its own public surface (which could double as the game's internal
interface). This resolves once the simulation's shape is known.

## License and lineage

Cathode is MIT. Its dependencies — quickjs-ng (MIT), litehtml (BSD), HarfBuzz (MIT-family), Blend2D
(zlib) — are all permissive; their notices are retained. No GPL code is incorporated. No Chromium,
no proprietary engine, no subscription. The plugin is forkable to the metal.
