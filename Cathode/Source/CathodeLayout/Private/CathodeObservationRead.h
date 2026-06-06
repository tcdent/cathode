#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

#include "CathodeLayout.h" // ILayoutDocumentSource, FLayoutNodeGeometry, FResolvedStyle

namespace Cathode
{

// The observation read-out — OUR bespoke logic backing getBoundingClientRect / getComputedStyle for the
// observer (NOT the author contract). Entirely litehtml-FREE: the WALK is a pure tree traversal of the
// neutral document source; the per-node box + per-property value are pulled through callbacks the engine
// binds to litehtml at the thin vendor edge (the boxes/cascade are vendor value, never hand-asserted).
//
// What we OWN + test here: (a) the walk visits every node and assembles a per-node FLayoutNodeGeometry;
// (b) the resolved-style mapping is BOUNDED to our contract property set (not the full CSSOM) and maps
// each property's value into its slot. What is the vendor edge: the box query + the raw computed-value
// query the engine binds to litehtml.

// The bounded set of CSS properties the observation read exposes (getComputedStyle is scoped to THIS set,
// not the full cascade). OURS — the contract surface the observer can read; the values come from litehtml.
const TArray<FString>& ContractStyleProperties();

// Map a node's resolved style by querying ONLY the contract property set: for each contract property,
// pull its computed value through GetValue and store it in its slot. Bounded by construction — a property
// outside ContractStyleProperties() is never queried nor stored. GetValue is the vendor edge (litehtml's
// computed cascade); this mapping is ours.
FResolvedStyle MapResolvedStyle(TFunctionRef<FString(const FString&)> GetValue);

// Walk the laid-out tree (root + children, via the neutral source) and assemble a per-node geometry
// record: each node's id + its box (via GetNodeBox) + its bounded resolved style (via GetNodeStyle). The
// traversal + assembly are ours; the box/style come through the callbacks (the vendor edge). Pre-order so
// parents precede descendants.
void WalkGeometry(const ILayoutDocumentSource& Source,
	TFunctionRef<FBox2f(int32)> GetNodeBox,
	TFunctionRef<FResolvedStyle(int32)> GetNodeStyle,
	TArray<FLayoutNodeGeometry>& Out);

} // namespace Cathode
