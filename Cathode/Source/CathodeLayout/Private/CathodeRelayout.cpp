#include "CathodeRelayout.h"

namespace Cathode
{

bool FDirtyTracker::HasLayoutDirty() const
{
	for (const FDirtyMark& Mark : Marks)
	{
		if (Mark.Kind != EDirtyKind::PaintOnly)
		{
			return true;
		}
	}
	return false;
}

int32 FindLayoutBoundary(const ILayoutDocumentSource& Tree, int32 NodeId)
{
	int32 Current = NodeId;
	while (Current != INDEX_NONE)
	{
		if (Tree.IsLayoutBoundary(Current))
		{
			return Current;
		}
		Current = Tree.GetParentNode(Current);
	}
	return Tree.GetRootNode(); // no boundary up the chain -> the root is the boundary (full relayout)
}

FRelayoutPlan ComputeRelayoutPlan(const ILayoutDocumentSource& Tree, const TArray<FDirtyMark>& Marks)
{
	FRelayoutPlan Plan;
	const int32 Root = Tree.GetRootNode();
	TSet<int32> Roots;

	for (const FDirtyMark& Mark : Marks)
	{
		if (Mark.Kind == EDirtyKind::PaintOnly)
		{
			continue; // paint-only changes skip layout
		}

		const int32 Boundary = FindLayoutBoundary(Tree, Mark.NodeId);
		if (Boundary == Root || Boundary == INDEX_NONE)
		{
			Plan.bFull = true; // a root-level boundary means a complete re-feed
			break;
		}
		Roots.Add(Boundary);
	}

	if (Plan.bFull)
	{
		Plan.SubtreeRoots.Reset(); // full subsumes any subtree roots
	}
	else
	{
		Plan.SubtreeRoots = Roots.Array();
	}
	return Plan;
}

FRelayoutPlan FRelayoutController::Relayout(int32 AvailableWidth, IRelayoutBackend& Backend)
{
	FRelayoutPlan Plan;

	if (!bEverLaidOut || DocumentSource == nullptr)
	{
		// First layout (or no source tree yet) -> complete re-feed.
		Plan.bFull = true;
	}
	else if (!Tracker.HasAnyDirty())
	{
		return Plan; // nothing changed -> no relayout, no backend call
	}
	else
	{
		Plan = ComputeRelayoutPlan(*DocumentSource, Tracker.GetMarks());
		if (!Plan.RequiresRelayout())
		{
			// Only PaintOnly marks -> no relayout (the paint region is handled separately).
			Tracker.Flush();
			bEverLaidOut = true;
			return Plan;
		}
	}

	if (Plan.bFull)
	{
		Backend.RelayoutDocument(AvailableWidth);
	}
	else
	{
		for (int32 Root : Plan.SubtreeRoots)
		{
			// Scaffold: constrain the subtree to the available width (the boundary's containing-block width
			// refines at the validation pass). The request SCOPE is what the spec asserts.
			Backend.RelayoutSubtree(Root, AvailableWidth);
		}
	}

	Tracker.Flush();
	bEverLaidOut = true;
	return Plan;
}

} // namespace Cathode
