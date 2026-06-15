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

// Minimal text measurer for driving the REAL litehtml engine (T3) — block layout with explicit inline sizes
// barely exercises it (the fixture has no text content).
class FStubMeasurer final : public ITextMeasurer
{
public:
	virtual FLayoutFontRef ResolveFont(const FString&, int32, int32, bool) override { return FLayoutFontRef(); }
	virtual float MeasureWidth(const FString&, FLayoutFontRef) override { return 0.0f; }
	virtual void GetFontMetrics(FLayoutFontRef, float& OutAscent, float& OutDescent, float& OutHeight) override
	{
		OutAscent = 12.0f;
		OutDescent = 4.0f;
		OutHeight = 16.0f;
	}
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

	// Serialization now emits the data-node-id correlation handle (sprint 011 [c] T1) FIRST on each element,
	// before the sorted author attributes (it is what the layout engine reads back to map litehtml's placed
	// element to our NodeId). Built with the ACTUAL NodeIds (not hard-coded) so the match stays exact.
	TestEqual(TEXT("GetDocumentHtml serializes the whole tree (with data-node-id)"),
		Doc->GetDocumentHtml(),
		FString::Printf(TEXT("<html data-node-id=\"%d\"><div data-node-id=\"%d\" id=\"a\">hi</div></html>"),
			Doc->GetRootNode(), DivId));
	TestEqual(TEXT("GetSubtreeHtml serializes the subtree (with data-node-id)"),
		Doc->GetSubtreeHtml(DivId),
		FString::Printf(TEXT("<div data-node-id=\"%d\" id=\"a\">hi</div>"), DivId));
	TestEqual(TEXT("GetDocumentCss is empty (CSS sourcing deferred)"), Doc->GetDocumentCss(), FString());

	// Escaping: special chars in text + attribute value are HTML-escaped (& first); data-node-id precedes them.
	FJsValue EscW = Doc->CreateElement(TEXT("p"));
	FDomNode* Esc = Doc->NodeFromWrapper(EscW);
	Esc->SetAttribute(TEXT("title"), TEXT("a<b&c\"d"));
	Esc->SetTextContent(TEXT("x<y&z>"));
	TestEqual(TEXT("text + attribute escaped (data-node-id precedes the author attrs)"),
		Doc->GetSubtreeHtml(Esc->GetNodeId()),
		FString::Printf(TEXT("<p data-node-id=\"%d\" title=\"a&lt;b&amp;c&quot;d\">x&lt;y&amp;z&gt;</p>"),
			Esc->GetNodeId()));

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

// Sprint 011 [c] target 3 — REAL OBSERVATION GEOMETRY (the come-due discharge).
//
// Drives the DOM observation READ path (FDomNode::GetBoundingClientRect) over the REAL FLiteHtmlLayoutEngine
// (NOT FScriptedLayoutEngine): a real FDomDocument tree — whose SerializeNode emits the data-node-id handle —
// is laid out by real litehtml, and the composed RECT each node's getBoundingClientRect returns is litehtml's
// OWN box. Per the oracle, asserts the observation READ's own output (the composed rect), proving the READ
// PATH, with RELATIONAL invariants only real layout produces (non-zero; child nests in parent; siblings stack
// at increasing Y; parent encloses) — NOT a re-assert of the raw engine box, NOT a hand-typed pixel.
// checked = the observation records whose composed rect traces to litehtml's real layout.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomObservationRealLayoutTest,
	"Cathode.Dom.Observation.RealLayoutGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomObservationRealLayoutTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);

	// The REAL litehtml engine drives the DOM's layout (the FDomDocument ctor calls Layout.SetDocument(*this)).
	FStubMeasurer Measurer;
	TUniquePtr<ILayoutEngine> RealEngine = CreateLiteHtmlLayoutEngine(Measurer);
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, *RealEngine);

	// A real sized tree under the root: container(200px) -> [a(50x30), b(60x40)] (inline styles -> litehtml).
	FJsValue ContainerW = Doc->CreateElement(TEXT("div"));
	FDomNode* Container = Doc->NodeFromWrapper(ContainerW);
	Container->SetAttribute(TEXT("style"), TEXT("width:200px"));
	Doc->FindNode(Doc->GetRootNode())->AppendChild(ContainerW);

	FJsValue AW = Doc->CreateElement(TEXT("div"));
	FDomNode* A = Doc->NodeFromWrapper(AW);
	A->SetAttribute(TEXT("style"), TEXT("width:50px;height:30px"));
	Container->AppendChild(AW);

	FJsValue BW = Doc->CreateElement(TEXT("div"));
	FDomNode* B = Doc->NodeFromWrapper(BW);
	B->SetAttribute(TEXT("style"), TEXT("width:60px;height:40px"));
	Container->AppendChild(BW);

	// Drive REAL litehtml layout (serializer data-node-id -> createFromString -> render -> the NodeId map) + cache.
	Doc->Relayout(800);

	// The observation READ path: each node's composed rect surfaced through getBoundingClientRect.
	FBox2f BoxC, BoxA, BoxB;
	if (!Container->GetBoundingClientRect(BoxC) || !A->GetBoundingClientRect(BoxA) || !B->GetBoundingClientRect(BoxB))
	{
		AddError(TEXT("the observation read returned no rect for a laid-out node -- real geometry did not flow through the read path"));
		return false;
	}

	// ORACLE: relational invariants on the COMPOSED rects (the read path's output), from litehtml's real layout.
	TestTrue(TEXT("child a has a non-zero composed rect"), BoxA.GetSize().X > 0.0f && BoxA.GetSize().Y > 0.0f);
	TestTrue(TEXT("child b has a non-zero composed rect"), BoxB.GetSize().X > 0.0f && BoxB.GetSize().Y > 0.0f);
	TestTrue(TEXT("child a nests within the container's x-extent (read path)"), BoxA.Min.X >= BoxC.Min.X && BoxA.Max.X <= BoxC.Max.X);
	TestTrue(TEXT("sibling rects stack at increasing Y (read through getBoundingClientRect)"), BoxB.Min.Y >= BoxA.Max.Y);
	TestTrue(TEXT("the container encloses its stacked children (read path)"), BoxC.GetSize().Y >= BoxA.GetSize().Y + BoxB.GetSize().Y);

	// A node absent from the laid-out result still reads false — the read COMPOSES the real result, not a blanket.
	FJsValue OrphanW = Doc->CreateElement(TEXT("span"));
	FBox2f OrphanBox;
	TestFalse(TEXT("a node not in the layout result reads false"),
		Doc->NodeFromWrapper(OrphanW)->GetBoundingClientRect(OrphanBox));

	AddInfo(TEXT("[CATHODE-T3] 3 observation rects traced to litehtml's real layout through getBoundingClientRect"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
