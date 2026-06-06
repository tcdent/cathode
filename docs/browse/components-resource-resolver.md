# Cathode — Resource Resolver (detail)

Detail for the **Resource Resolver** component (see `components.md`). What/why, not how.

Screen assets are bundled in the game, not served over a network. This component is the one place
that maps logical URLs to bundled content, so neither the layout engine nor the JS loader ever
needs an HTTP server.

## Responsibilities

**Resolve layout resources.** Answer litehtml's requests for images, linked CSS, and fonts by
returning bundled content. *Why:* litehtml doesn't fetch on its own — it asks the host — and an
internal HTTP server to serve files we already hold in memory would be pure ceremony. *Ground:*
litehtml resource callbacks.

**Resolve JS/module loads.** Provide author JavaScript (and any data it loads) from the bundle.
*Why:* the JS engine's module loading is host-provided; this is where it points. *Ground:* author
JS is pre-bundled to a single file, so module resolution is trivial.

**Define the URL namespace.** Map a virtual root to bundled paths so authors write ordinary
relative URLs (`<img src="logo.png">`, `@import`). *Why:* it keeps screens authorable as normal
web pages with no Cathode-specific URL ceremony.

## Boundaries

Owns URL→bundle resolution. Serves Layout & Style (resource callbacks) and the JS loader. Does no
network I/O. Does not decide bundling/packaging itself — it reads from wherever assets are bundled.

## Open / deferred (team's call)

- The virtual URL scheme/root.
- Where assets live (UE pak/Content vs a plugin content directory) and how they're addressed.
- Any caching of resolved resources.
