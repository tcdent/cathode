// Cathode CSS-selector matching — the SWAP-BOUNDARY interface (sprint 012 selectors).
//
// litehtml owns CSS-selector PARSING (css_selector::parse — no selector-set drift); the MATCH runs over the LIVE
// retained DOM tree, NOT litehtml's per-layout throwaway element tree. This header is NEUTRAL by contract: it
// exposes ONLY engine-neutral types (FString / TArray / the abstract IMatchableNode) — NO litehtml type appears
// here, so the DOM module (CathodeDom) consumes it with NO litehtml link and no litehtml-header recompile coupling.
// The parser + matcher live entirely in the impl (CathodeSelectorService.cpp), the same swap-boundary discipline as
// CathodeLayout.h: litehtml's types appear ONLY in the .cpp.
//
// Why match the LIVE tree (the load-bearing trap): a matcher reading litehtml's element tree would see a STALE
// snapshot from the last serialized-markup relayout — a DOM mutation not yet laid out would be missed. The matcher
// walks IMatchableNode (implemented by FDomNode), so a freshly-mutated node is matched WITHOUT a relayout.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

namespace Cathode
{

// A DOM node the selector matcher can read STRUCTURALLY. FDomNode implements this directly, so the matcher walks
// the live retained tree. Neutral surface only — tag / attributes / parent / children; no litehtml, no quickjs.
class IMatchableNode
{
public:
	virtual ~IMatchableNode() = default;

	// The element tag name (e.g. "div"); "" for non-element nodes (text), which never match a type/condition.
	virtual FString GetMatchTagName() const = 0;
	// The value of attribute Name (false if absent). #id / .class derive from the 'id' / 'class' attributes here.
	virtual bool GetMatchAttribute(const FString& Name, FString& OutValue) const = 0;
	// The structural parent, or null at the tree root. Used for the descendant (' ') + child ('>') combinators.
	virtual const IMatchableNode* GetMatchParent() const = 0;
	// The child ELEMENTS in document order (text children omitted — they never match a selector). Pre-order walk.
	virtual void GetMatchChildren(TArray<const IMatchableNode*>& OutChildren) const = 0;
};

enum class ESelectorStatus : uint8
{
	Matched,         // a match was produced (OutMatch / OutMatches / bOutMatches set accordingly)
	NoMatch,         // a VALID selector that matched nothing (OutMatch null / list empty / bool false)
	InvalidSelector, // unparseable OR uses an UNSUPPORTED feature -> the caller throws a DOM SyntaxError (never a
	                 // silent mis-match: an unsupported-but-parseable selector would false-pass author code otherwise)
};

// Parse a CSS selector (litehtml-grounded) + match it against the live IMatchableNode tree. CathodeDom holds this
// by ABSTRACT reference (no litehtml crosses the seam). COMPOSE-NOT-DUPLICATE: all query shapes run the ONE parsed
// selector through the ONE structural match primitive — no matcher-per-shape (no xerox degradation).
//
// SUPPORTED SET (litehtml-grounded, NOT fabricated): type/`*`, `.class`, `#id`, `[attr]` (+ `=` `~=` `|=` `^=`
// `$=` `*=`, with litehtml's own value semantics), the descendant (' ') + child ('>') combinators, and `:not(simple)`.
// Anything else parseable by litehtml (sibling `+`/`~`, `:nth-*`, other pseudo-classes, pseudo-elements) is reported
// InvalidSelector — declared-unsupported, never silently mis-matched.
class ISelectorService
{
public:
	virtual ~ISelectorService() = default;

	// First element in PRE-ORDER matching Selector. Root is tested iff bIncludeRoot (document.querySelector passes
	// the root element + true; element.querySelector passes the element + false = EXCLUDE-SELF, descendants only).
	// Combinators walk the candidate's REAL ancestors (which may sit above Root) — correct per spec.
	virtual ESelectorStatus QueryFirst(const IMatchableNode& Root, bool bIncludeRoot, const FString& Selector,
		const IMatchableNode*& OutMatch) const = 0;

	// NOTE: querySelectorAll / matches / closest (the TRIO) extend THIS interface over the SAME parsed-selector +
	// match primitive (compose-not-duplicate) when the contract confirms — added with their bindings + proof so the
	// matcher is never re-opened. Held out here only to avoid unbound/unproven surface ahead of that confirm.
};

// The production matcher (litehtml-backed parse + live-tree match). Stateless + container-free — it needs no
// litehtml document/container, only css_selector::parse + a structural walk over IMatchableNode.
CATHODELAYOUT_API TUniquePtr<ISelectorService> CreateSelectorService();

} // namespace Cathode
