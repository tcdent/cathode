#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for the layout handshake — ILayoutDocumentSource structure reads + the MARKUP SERIALIZATION the whole
// Model-A design rests on — and the observation reads composing 005's geometry. Assert OUR bookkeeping: the
// serialized string for a hand-built tree, the structure NodeIds, and that the observation reads look up the
// right NodeId's FLayoutNodeGeometry (geometry VALUES scripted, standing in for 005's; real layout deferred
// to validation-pass). ZERO real-layout / litehtml-internal assertions. Authored, not run this sprint.

namespace
{
using namespace Cathode;

// A scripted layout engine: SetDocument/MarkNodeDirty are no-ops; Relayout returns a scripted FLayoutResult so
// the observation reads have geometry to compose (standing in for real 005 layout output).
class FScriptedLayoutEngine final : public ILayoutEngine
{
public:
	FLayoutResult Scripted;
	virtual void SetDocument(const ILayoutDocumentSource&) override {}
	virtual void MarkNodeDirty(int32, EDirtyKind) override {}
	virtual FLayoutResult Relayout(int32) override { return Scripted; }
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomSerializeTest,
	"Cathode.Dom.Layout.MarkupSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomSerializeTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FScriptedLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout); // auto-creates an <html> root

	// root(html) -> div(id="a") -> text("hi")
	FJsValue DivW = Doc->CreateElement(TEXT("div"));
	FDomNode* Div = Doc->NodeFromWrapper(DivW);
	const FDomNodeId DivId = Div->GetNodeId();
	Div->SetAttribute(TEXT("id"), TEXT("a"));
	Div->SetTextContent(TEXT("hi"));
	Doc->FindNode(Doc->GetRootNode())->AppendChild(DivW);

	TestEqual(TEXT("GetDocumentHtml serializes the whole tree"),
		Doc->GetDocumentHtml(), FString(TEXT("<html><div id=\"a\">hi</div></html>")));
	TestEqual(TEXT("GetSubtreeHtml serializes the subtree"),
		Doc->GetSubtreeHtml(DivId), FString(TEXT("<div id=\"a\">hi</div>")));
	TestEqual(TEXT("GetDocumentCss is empty (CSS sourcing deferred)"), Doc->GetDocumentCss(), FString());

	// Escaping: special chars in text + attribute value are HTML-escaped (& first).
	FJsValue EscW = Doc->CreateElement(TEXT("p"));
	FDomNode* Esc = Doc->NodeFromWrapper(EscW);
	Esc->SetAttribute(TEXT("title"), TEXT("a<b&c\"d"));
	Esc->SetTextContent(TEXT("x<y&z>"));
	TestEqual(TEXT("text + attribute escaped"),
		Doc->GetSubtreeHtml(Esc->GetNodeId()),
		FString(TEXT("<p title=\"a&lt;b&amp;c&quot;d\">x&lt;y&amp;z&gt;</p>")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomStructureReadsTest,
	"Cathode.Dom.Layout.StructureReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomStructureReadsTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FScriptedLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);

	const FDomNodeId RootId = Doc->GetRootNode();
	FJsValue DivW = Doc->CreateElement(TEXT("div"));
	const FDomNodeId DivId = Doc->NodeFromWrapper(DivW)->GetNodeId();
	Doc->FindNode(RootId)->AppendChild(DivW);

	// The 005 structure reads answered from the wrapper-governed index, by NodeId.
	TestTrue(TEXT("GetRootNode is the root"), RootId != INDEX_NONE);
	TestEqual(TEXT("GetParentNode(div) is the root"), Doc->GetParentNode(DivId), RootId);
	{
		TArray<int32> Kids;
		Doc->GetChildNodes(RootId, Kids);
		TestTrue(TEXT("root's children include the div"), Kids.Contains(DivId));
	}
	TestTrue(TEXT("root is a layout boundary"), Doc->IsLayoutBoundary(RootId));
	TestFalse(TEXT("a non-root node is not a boundary (conservative)"), Doc->IsLayoutBoundary(DivId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomObservationTest,
	"Cathode.Dom.Observation.ReadsComposeGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomObservationTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FScriptedLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);

	FJsValue DivW = Doc->CreateElement(TEXT("div"));
	FDomNode* Div = Doc->NodeFromWrapper(DivW);
	const FDomNodeId DivId = Div->GetNodeId();
	Doc->FindNode(Doc->GetRootNode())->AppendChild(DivW);

	// Script 005's geometry for the div (the box/style VALUES stand in for real layout output).
	FLayoutNodeGeometry Geom;
	Geom.NodeId = DivId;
	Geom.Box = FBox2f(FVector2f(1, 2), FVector2f(11, 12));
	Geom.Style.Properties.Add(TEXT("width"), TEXT("10px"));
	Layout.Scripted.Geometry.Add(Geom);

	Doc->Relayout(800); // caches the scripted result

	// The observation reads COMPOSE 005's geometry by NodeId (our lookup; the values are 005's).
	FBox2f Box;
	TestTrue(TEXT("getBoundingClientRect finds the laid-out box"), Div->GetBoundingClientRect(Box));
	TestTrue(TEXT("the box is the node's geometry"), Box == FBox2f(FVector2f(1, 2), FVector2f(11, 12)));

	FResolvedStyle Style;
	TestTrue(TEXT("getComputedStyle finds the resolved style"), Div->GetComputedStyle(Style));
	const FString* Width = Style.Properties.Find(TEXT("width"));
	TestTrue(TEXT("computed style carries the bounded property"), Width != nullptr && *Width == FString(TEXT("10px")));

	// A node with no laid-out geometry reads false (no entry in the result).
	FJsValue OtherW = Doc->CreateElement(TEXT("span"));
	FBox2f OtherBox;
	TestFalse(TEXT("a node not in the layout result reads false"),
		Doc->NodeFromWrapper(OtherW)->GetBoundingClientRect(OtherBox));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
