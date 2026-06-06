#pragma once

#include "CoreMinimal.h"

#include "CathodeLayout.h" // EDirtyKind, ILayoutDocumentSource

namespace Cathode
{

struct FDirtyMark
{
	int32 NodeId = INDEX_NONE;
	EDirtyKind Kind = EDirtyKind::Layout;
};

// Dirty tracking — OUR bookkeeping (litehtml-free). Accumulate marks until a relayout flushes them.
class FDirtyTracker
{
public:
	void Mark(int32 NodeId, EDirtyKind Kind) { Marks.Add(FDirtyMark{ NodeId, Kind }); }

	const TArray<FDirtyMark>& GetMarks() const { return Marks; }
	bool HasAnyDirty() const { return Marks.Num() > 0; }
	// Any mark that requires a relayout (Layout/Structure); PaintOnly does not.
	bool HasLayoutDirty() const;

	void Flush() { Marks.Reset(); }

private:
	TArray<FDirtyMark> Marks;
};

// The relayout plan: a complete re-feed (bFull) OR a set of affected-subtree roots (intelligent path).
struct FRelayoutPlan
{
	bool bFull = false;
	TArray<int32> SubtreeRoots;

	bool RequiresRelayout() const { return bFull || SubtreeRoots.Num() > 0; }
};

// AFFECTED-SUBTREE SELECTION — the bespoke intelligence, a PURE graph computation on the retained tree
// (litehtml-free). For each Layout/Structure mark, walk UP to the nearest layout-boundary ancestor; that
// boundary is the relayout root. A boundary that is the document root => a full re-feed. PaintOnly marks
// add nothing (they skip layout). Roots are de-duplicated; a full plan subsumes subtree roots.
FRelayoutPlan ComputeRelayoutPlan(const ILayoutDocumentSource& Tree, const TArray<FDirtyMark>& Marks);

// Walk up from a node to the nearest layout-boundary ancestor (the relayout root). Returns the document
// root if none is found.
int32 FindLayoutBoundary(const ILayoutDocumentSource& Tree, int32 NodeId);

// The relayout backend — the seam that actually re-lays-out (the litehtml edge). The litehtml impl
// re-feeds + renders; a recording impl records WHICH scope was requested (for the invocation tests).
class IRelayoutBackend
{
public:
	virtual ~IRelayoutBackend() = default;

	/** Complete re-feed: relayout the whole document at the available width. */
	virtual void RelayoutDocument(int32 AvailableWidth) = 0;

	/** Intelligent re-feed-of-subtree: relayout only the subtree rooted at SubtreeRoot, constrained to its
	 *  containing-block width. */
	virtual void RelayoutSubtree(int32 SubtreeRoot, int32 ContainingWidth) = 0;
};

// The relayout controller — OUR orchestration (litehtml-free): owns dirty tracking + the document source,
// computes the plan (selection), and DRIVES the backend (complete vs intelligent), then flushes. The
// engine composes it with the litehtml backend; the specs compose it with a recording backend.
class FRelayoutController
{
public:
	void SetDocument(const ILayoutDocumentSource& Source) { DocumentSource = &Source; }
	void MarkNodeDirty(int32 NodeId, EDirtyKind Kind) { Tracker.Mark(NodeId, Kind); }

	/** Drive the backend per the computed plan, then flush. Returns the plan driven (for inspection). */
	FRelayoutPlan Relayout(int32 AvailableWidth, IRelayoutBackend& Backend);

private:
	const ILayoutDocumentSource* DocumentSource = nullptr;
	FDirtyTracker Tracker;
	bool bEverLaidOut = false;
};

} // namespace Cathode
