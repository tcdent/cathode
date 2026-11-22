#include "CathodeObservationRead.h"

namespace Cathode
{

const TArray<FString>& ContractStyleProperties()
{
	// The bounded getComputedStyle surface — the box-model + visual properties an observer needs, NOT the
	// full CSSOM. Adding a property here is a deliberate widening of the read contract.
	static const TArray<FString> Properties = {
		TEXT("display"),
		TEXT("position"),
		TEXT("width"),
		TEXT("height"),
		TEXT("top"),
		TEXT("left"),
		TEXT("margin"),
		TEXT("padding"),
		TEXT("border-width"),
		TEXT("color"),
		TEXT("background-color"),
		TEXT("font-family"),
		TEXT("font-size"),
		TEXT("font-weight"),
		TEXT("line-height"),
		TEXT("text-align"),
		TEXT("visibility"),
		TEXT("opacity"),
		TEXT("letter-spacing"), // CathodeComputed — QueryComputedValue parses the own-inline value
	};
	return Properties;
}

FResolvedStyle MapResolvedStyle(TFunctionRef<FString(const FString&)> GetValue)
{
	FResolvedStyle Style;
	const TArray<FString>& Contract = ContractStyleProperties();
	Style.Properties.Reserve(Contract.Num());
	for (const FString& Property : Contract)
	{
		// Bounded: only contract properties are queried, and each lands in its own slot.
		Style.Properties.Add(Property, GetValue(Property));
	}
	return Style;
}

void WalkGeometry(const ILayoutDocumentSource& Source,
	TFunctionRef<FBox2f(int32)> GetNodeBox,
	TFunctionRef<FResolvedStyle(int32)> GetNodeStyle,
	TArray<FLayoutNodeGeometry>& Out)
{
	const int32 Root = Source.GetRootNode();
	if (Root == INDEX_NONE)
	{
		return;
	}

	// Explicit stack for a pre-order DFS (parents before descendants); no recursion depth assumption.
	TArray<int32> Stack;
	Stack.Push(Root);

	TArray<int32> Children;
	while (Stack.Num() > 0)
	{
		const int32 NodeId = Stack.Pop();

		FLayoutNodeGeometry Geometry;
		Geometry.NodeId = NodeId;
		Geometry.Box = GetNodeBox(NodeId);
		Geometry.Style = GetNodeStyle(NodeId);
		Out.Add(MoveTemp(Geometry));

		Children.Reset();
		Source.GetChildNodes(NodeId, Children);
		// Push in reverse so the first child is visited next (stable left-to-right pre-order).
		for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
		{
			Stack.Push(Children[Index]);
		}
	}
}

} // namespace Cathode
