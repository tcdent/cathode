#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

// Cathode Resource Resolver — the one place that maps logical URLs to BUNDLED content, so neither the layout
// engine nor the JS loader ever needs an HTTP server. SMALL + PURE-BESPOKE: it wraps no vendor and
// COUPLES-TO-NONE — it DEFINES the neutral seams; 005 (litehtml resource callbacks), 002 (author-JS bundle),
// and 004 (url-addressed font bytes) are the CONSUMERS. No network I/O, ever.
//
// Dependency direction (RQ2): the resolver defines IResourceResolver + IBundleSource here; 005 IMPORTS them
// (005 -> CathodeResolver dep, the analog of the DOM importing 005's ILayoutDocumentSource). The resolver
// imports no sibling, no vendor.
//
// SHAPES ONLY this slice (Slice 1) — seams + the URL-namespace + the configurable-root shape; bodies + the
// 005-hook APPLY land in Slice 2.

namespace Cathode
{

// The neutral bundle backend: a root-relative bundle PATH -> bytes, hit/miss. The resolver's URL logic is
// backend-agnostic; the UE-asset backend (content-dir + UFS staging + FFileHelper, rooted at the configurable
// /Game/Cathode/) implements this at INTEGRATION; tests use an in-memory source. This is the ONLY place the
// bundle LOCATION lives — the configured root is the backend's (the resolver works in root-relative space).
class IBundleSource
{
public:
	virtual ~IBundleSource() = default;

	// Read a normalized, root-relative bundle path (e.g. "img/logo.png", no leading '/') into bytes. Returns
	// false on a MISS — a clean not-found (trusted-only: no network, no path-traversal handling/theater).
	virtual bool Read(const FString& BundlePath, TArray<uint8>& OutBytes) const = 0;
};

// The resource-resolution seam the consumers call. Type-AGNOSTIC (url -> bytes/text): the consumer knows the
// resource KIND. 005's document_container: images + url-fonts -> ResolveBytes; linked CSS (import_css) ->
// ResolveText. 002: the author-JS bundle -> ResolveBytes. 004: url-addressed font bytes -> ResolveBytes.
//
// RQ1 (image-size): the resolver does NOT decode — it returns BYTES, never dimensions. Decoding to answer
// get_image_size would couple it to a decoder (003/Blend2D); to stay couples-to-none, get_image_size's
// dimensions are resolved CONSUMER-SIDE (003 decodes the resolver's bytes) at integration. The resolver has
// no size method. (Settled by the couples-to-none constraint — not a human fork; see the report's note on the
// get_image_size -> 003-decode consequence.)
class IResourceResolver
{
public:
	virtual ~IResourceResolver() = default;

	// Resolve a logical URL (against BaseUrl) to bytes — images, url-fonts, the author-JS bundle, any binary.
	// Normalizes Url (relative vs BaseUrl's dir; '/'-rooted vs the virtual root) to a root-relative path, then
	// reads via IBundleSource. Returns false on a miss.
	virtual bool ResolveBytes(const FString& Url, const FString& BaseUrl, TArray<uint8>& OutBytes) const = 0;

	// Resolve a logical URL to UTF-8 text — for litehtml import_css (linked CSS). (ResolveBytes + UTF-8
	// decode.) Returns false on a miss.
	virtual bool ResolveText(const FString& Url, const FString& BaseUrl, FString& OutText) const = 0;
};

// The bespoke URL-namespace logic (public + testable): normalize a logical URL into a root-relative bundle
// path. A '/'-rooted Url maps to the VIRTUAL ROOT; a relative Url resolves against BaseUrl's directory; "./"
// and "../" segments collapse. Returns a root-relative path (no leading '/'). NO custom author scheme —
// authors write ordinary web URLs. (Body Slice 2.)
CATHODERESOLVER_API FString ResolveUrlPath(const FString& Url, const FString& BaseUrl);

// Construct the resolver over a bundle source. The Source carries the CONFIGURED ROOT (default /Game/Cathode/,
// expected to be changed by most users) — its config (a project setting / UDeveloperSettings) is integration;
// the resolver works in root-relative virtual space, the Source maps a root-relative path to
// ProjectContentDir()/<root>/path. (RQ3 scaffold: the configured root is the Source's input.) Body Slice 2.
CATHODERESOLVER_API TUniquePtr<IResourceResolver> CreateResourceResolver(IBundleSource& Source);

// === 005 document_container HOOK CONTRACT (specified here; APPLIED to CathodeLayout in Slice 2) ===
//
// 005's ILayoutEngine gains exactly one nullable setter (non-pure default no-op, so 008's ILayoutEngine test
// stubs + the no-resolver path stay byte-identical):
//
//     virtual void SetResourceResolver(IResourceResolver* Resolver) {}   // in ILayoutEngine (CathodeLayout.h)
//
// FLiteHtmlLayoutEngine overrides it (stores the resolver, passes it to the backend/container).
// FCathodeDocumentContainer holds an IResourceResolver* and routes its TODO(Resource Resolver) stubs to it
// when non-null (else today's no-op):
//   - load_image(src, baseurl, ...)        -> Resolver->ResolveBytes(src, baseurl, ...) (bytes for the draw)
//   - get_image_size(src, baseurl, &sz)    -> bytes via ResolveBytes, then DIMENSIONS via a 003 decode
//                                             (consumer-side, integration; until then 0x0 — the clean miss)
//   - import_css(text, url, baseurl)       -> Resolver->ResolveText(url, baseurl, text)
// This makes CathodeLayout import CathodeResolver (005 -> CathodeResolver dep, the blessed evolution); 005's
// rest stays byte-identical. (Null resolver == today's stub behavior.)

} // namespace Cathode
