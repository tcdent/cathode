#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

// Cathode Resource Resolver — the one place that maps logical URLs to BUNDLED content, so neither the layout
// engine nor the JS loader ever needs an HTTP server. SMALL + PURE-BESPOKE: it wraps no vendor and
// COUPLES-TO-NONE — it DEFINES the neutral seams; CathodeLayout (litehtml resource callbacks), CathodeRuntime
// (author-JS bundle), and CathodeText (url-addressed font bytes) are the CONSUMERS. No network I/O, ever.
//
// Dependency direction: the resolver defines IResourceResolver + IBundleSource here; CathodeLayout IMPORTS them
// (CathodeLayout -> CathodeResolver dep, the analog of the DOM importing CathodeLayout's ILayoutDocumentSource).
// The resolver imports no sibling, no vendor.

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
// resource KIND. CathodeLayout's document_container: images + url-fonts -> ResolveBytes; linked CSS (import_css) ->
// ResolveText. CathodeRuntime: the author-JS bundle -> ResolveBytes. CathodeText: url-addressed font bytes ->
// ResolveBytes.
//
// Image size: the resolver does NOT decode — it returns BYTES, never dimensions. Decoding to answer
// get_image_size would couple it to a decoder (CathodeRasterizer/Blend2D); to stay couples-to-none,
// get_image_size's dimensions are resolved CONSUMER-SIDE (CathodeRasterizer decodes the resolver's bytes). The
// resolver has no size method.
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
// authors write ordinary web URLs.
CATHODERESOLVER_API FString ResolveUrlPath(const FString& Url, const FString& BaseUrl);

// Construct the resolver over a bundle source. The Source carries the CONFIGURED ROOT (default /Game/Cathode/,
// expected to be changed by most users) — its config (a project setting / UDeveloperSettings) is integration;
// the resolver works in root-relative virtual space, the Source maps a root-relative path to
// ProjectContentDir()/<root>/path. (The configured root is the Source's input.)
CATHODERESOLVER_API TUniquePtr<IResourceResolver> CreateResourceResolver(IBundleSource& Source);

// The PRODUCTION read-mechanism backend: an IBundleSource that reads REAL file bytes via FFileHelper,
// rooted at a SUPPLIED (REQUIRED) RootDir — Read(path) -> FFileHelper::LoadFileToArray over RootDir/path; false +
// empty OutBytes on a clean miss. The FFontRegistry loader is the precedent. The RootDir is the configured root: NO
// baked production default-root here — the production default-root factory + the UFS-staging config + the cooked-pak
// read validation are the COUPLED integration item (COVERAGE.md Deferred #3/#5, shipped together). Mirrors
// CreateResourceResolver's factory shape; the impl class is private to its TU.
CATHODERESOLVER_API TUniquePtr<IBundleSource> CreateUEFileBundleSource(const FString& RootDir);

// === CathodeLayout document_container HOOK CONTRACT (specified here; applied in CathodeLayout) ===
//
// CathodeLayout's ILayoutEngine has exactly one nullable setter (non-pure default no-op, so CathodeDom's
// ILayoutEngine test stubs need not override it):
//
//     virtual void SetResourceResolver(IResourceResolver* Resolver) {}   // in ILayoutEngine (CathodeLayout.h)
//
// FLiteHtmlLayoutEngine overrides it (stores the resolver, passes it to the backend/container).
// FCathodeDocumentContainer holds an IResourceResolver* and routes its resource callbacks to it when
// non-null (else a clean miss):
//   - load_image(src, baseurl, ...)        -> Resolver->ResolveBytes(src, baseurl, ...) (bytes for the draw)
//   - get_image_size(src, baseurl, &sz)    -> bytes via ResolveBytes, then DIMENSIONS via a CathodeRasterizer
//                                             decode (consumer-side; 0x0 on a miss / undecodable bytes)
//   - import_css(text, url, baseurl)       -> Resolver->ResolveText(url, baseurl, text)
// This makes CathodeLayout import CathodeResolver (CathodeLayout -> CathodeResolver dep).

} // namespace Cathode
