# Cathode — Contract Feedback (panels ↔ engine)

> **Running log.** There is no finalized panels↔Cathode spec yet. These points come out of the
> back-and-forth as we establish the contract between the applications (the panels) and the engine
> — the observer / test script is the forcing function that surfaces them. They feed the spec as it
> formalizes. Status tags: **proposed** · **open** · **decided**.

## Round 1 — from pre-flighting the observer (`observe/observer.js`)

### DOM read surface is under-specified — **proposed**

The contract so far is mutation-heavy; the observer (and any real introspection) need *read-side*
APIs the surface didn't list. These are cheap (the tree already holds the data) and authoring them
into the DOM interface avoids false negatives where the observer uses an API Cathode never promised.
Add, deliberately:

- **tag name** (`nodeName`/`tagName`) — only `nodeType` was listed.
- **attribute enumeration** (read the set of attributes) — only `setAttribute`/`removeAttribute`.
- **text read** (`nodeValue`/`textContent` on text nodes) — text *mutation* was "optional"; *reading*
  it is needed.

And **remove `location.href`** from the observer — `location` is not supported (and shouldn't be);
it currently throws in Cathode before any snapshot is emitted.

### CSS animation vs JS timers — **RESOLVED → OUT** (settled in `spec-surface.md` Axis-3)

Do we support **CSS animations/transitions** (a litehtml fork) so authors express visual effects
declaratively, or keep them JS-timer-driven?

- **For style/visual effects** (blink, glow, fade, flicker): CSS animation is the cleaner idiom,
  and it's **freezable** for parity (Web Animations API → deterministic sampling) where JS timers
  are not — so it also fixes a class of false positives.
- **Cost:** litehtml doesn't support animations/transitions/`@keyframes` — adding them is a real
  fork (timeline, interpolation, keyframes), and animated screens then **re-render continuously**
  (the stress regime).
- **Distinction:** *content* effects (typewriter revealing text, live data updates) are inherently
  JS/DOM — CSS can't reveal text cleanly — so they stay JS regardless. The clean line:
  **CSS for style effects, JS for content/logic.** JS-driven content is correct (that's the runtime's
  job); only JS-driven *visual style* is the smell.

**RESOLVED (tcdent, 2026-06-15): OUT.** Animations / transitions / transforms are not supported — the
litehtml fork + continuous-render cost is not taken; it is settled in `spec-surface.md` Axis-3, not a
standing open decision. The Round 2/3 discussion below (animation idiom + the freeze hook) is retained
as history but **superseded** by this OUT ruling. (Content effects stay JS — unaffected.)

### Sandbox is a discovery baseline, not strict parity — **decided / noted**

The sandbox deliberately exceeds the subset (gap discovery) and contains time-varying JS content.
The dev added settling between animations; even so, JS-timed content (typewriter/cursor) isn't
freezable like CSS animations, so strict parity will see timing-driven differences (false
positives). For strict parity numbers, point the extractor at **deterministic content**; use the
sandbox for **gap discovery**.

---

## Round 2 — animation idiom + the freeze hook (from uipanel)

### CSS animation for visual style effects — **proposed (answers Round 1's open question)**

Support **CSS animations/transitions** for visual style effects (blink, glow, fade, flicker,
focus highlight); keep JS for content/logic. The deciding reason is parity, not just
authoring ergonomics:

- **Only CSS animations are deterministically freezable.** They run on a timeline the driver
  can pause + seek, so the observer samples a reproducible frame. JS-timer animation has no
  such handle (you'd have to mock the clock — fragile, engine-specific). Choosing JS timers
  for *style* would quietly break the parity loop the observer depends on.
- Secondary: declarative `@keyframes` beats babysitting `setInterval` + cleanup; animated CSS
  runs off the render loop (the JS thread isn't mutating the DOM every frame); LLMs author
  keyframes cleanly.

> "Using CSS animations makes maintaining this easier … CSS for animation feels more correct."

**The line:** CSS for visual *style*; JS for *content/logic*. JS-driven content (revealing
real text, live data) is correct and inherently JS — CSS can't reveal text. Only JS-driven
visual *style* is the smell.

**Reconciling the Round 1 note:** the sandbox cursor/typewriter are implemented as CSS
`@keyframes`, not JS timers — so they *are* freezable (the typewriter pins to `width:0`). The
current sandbox is in better parity shape than the earlier note assumed. A *true*
character-revealing typewriter would be JS/DOM and strict-parity-exempt; the CSS width-clip is
a style approximation and stays in-scope.

### The freeze hook — **proposed (engine line item, pairs with the above)**

If Cathode supports CSS animation, it also supports *freezing* it — the engine twin of the
driver's Web-Animations-API freeze. It is a clock intercept on the per-frame sampler the
engine already runs, not new interpolation logic:

- **Interface:** a host-side control the Cathode observe-driver calls — *not* a page JS API —
  `freezeAnimations(timeMs = 0)` / `resumeAnimations()`. Host-side keeps WAAPI off the
  contract (no need to implement `getAnimations()`/`Animation` in JS just for this).
- **Behavior:** seek + pause each **currently active** animation to `timeMs`, then run the
  sampler once (recompute style + layout) so `getComputedStyle` / `getBoundingClientRect`
  reflect the frozen frame. Declared properties (`animation-name`, …) are untouched, so gap
  discovery still sees them. At `t=0` every animation evaluates at its first keyframe / base.
- **Transitions vs. periodic — the one subtlety:** operate on the *active* set, not a global
  timeline rewind. Driver order: load → lay out → **settle ≈300ms** (transitions finish and
  drop out of the active set, holding their end state) → **freeze active animations at t**
  (periodic ones pin to phase t). That yields "transitions complete + animations at a known
  phase" in one deterministic frame, matching the Chromium oracle.
- **Offset is a knob:** default `t=0`; the same animation can later be sampled at several
  phases (0 / mid / end) and each diffed, to validate the curve and not just one frame.

Net: the animation fork's cost already includes the timeline; the freeze hook is a small,
deterministic read on top — and it is what makes animated parity possible at all. So it lands
as part of the animation feature, not as an afterthought.

---

*Next rounds append below as the observer runs against the real engine and the contract firms up.*
