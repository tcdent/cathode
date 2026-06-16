# Cathode — Input Routing (detail)

Detail for the **Input Routing** component (see `components.md`). What/why, not how.

Screens are interactive, and many surfaces share one runtime. This component gets input to the
**active surface** and manages **focus** within it. Input is keyboard today (and, later, the
controller's semantic events — see `components-controller.md`); it is **not** positional. Pointing
is deferred (see note below).

## Responsibilities

**Route input to the active surface.** Direct keyboard (and forthcoming semantic controller)
events to the surface that is currently active. *Why:* many surfaces share one runtime, so an
event must reach the correct document — and with focus-driven input the target is the *active*
surface, not wherever a pointer happens to be.

**Manage focus.** Track which element within the active surface holds focus, and move it in
response to directional / confirm / back input. *Why:* this is the navigation model — keystrokes
and semantic events need a target, and focus is how a screen receives them. Backed by
`element.focus()` and the `:focus` pseudo applied by the host.

**Dispatch as DOM events.** Deliver input through the DOM's event system (`keydown` on `document`
today; the controller's semantic events follow the same path). *Why:* author code expects the
standard web event model. (The controller component owns the *source* of semantic events; this
component owns getting them to the right surface and element.)

The ratified semantic set (see `components-controller.md`) is **six discrete intents** —
`Up` / `Down` / `Left` / `Right` / `Accept` / `Back` (no analog axis) — which this component
**delivers as standard synthetic `keydown` `KeyboardEvent`s** on the active surface's `document`,
the intents mapped to canonical `e.key` values (`Up`→`ArrowUp`, `Down`→`ArrowDown`,
`Left`→`ArrowLeft`, `Right`→`ArrowRight`, `Accept`→`Enter`, `Back`→`Escape`). Focus movement and
activation are then the panel's own stock `keydown` handlers plus our focus model — not a
Cathode-specific event. This is the consume side of the player `SurfaceFocus` handshake's
`DeliverSemanticEvent` (`docs/teams/player/006-surface-handshake-gap-analysis.md`): the player
resolves physical input to one discrete intent; Input Routing turns it into the standard keyboard
event the focused surface already expects. No `NavAxis` to discretize — the intent arrives already
discrete.

## Key concern — focus and the active surface

With positional input gone, the real work is "which surface is active, and which element within it
has focus, and how does navigation move that focus." That model — including behavior across
multiple surfaces — is the substance of this component.

## Boundaries

Owns active-surface targeting and focus. Uses Surfaces (which surface is active), DOM (dispatch +
focus state / `:focus`), and consumes keyboard input plus, later, the semantic events emitted by
the Controller component. Does not source or resolve input itself.

## Deferred — pointing

**Mouse/pointer input is deferred indefinitely** — the screens we're building navigate by keyboard
and focus, so we don't need it. This intentionally drops element-at-point **hit-testing**, pointer
event translation, the **world→surface coordinate mapping**, and any cursor model.

It is not ruled out forever — it could land on the project roadmap later. If it does, it
reintroduces: element-at-point hit-testing (from Layout & Style), a host-supplied world→surface
coordinate mapping (3D interaction → 2D surface point), and pointer event dispatch. None of that is
in scope now.

## Open / deferred (team's call)

- Focus behavior across multiple active surfaces.
- (Pointing — deferred indefinitely, per the note above.)
