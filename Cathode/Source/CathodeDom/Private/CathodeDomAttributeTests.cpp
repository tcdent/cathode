#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"

#include "Misc/AutomationTest.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Specs for attribute/property access + the 007-backed value marshaling. Drive the C++ methods directly;
// assert OUR bookkeeping (round-trips, dirty-marks, converted values, exotic rejection). ZERO GC-internal /
// refcount / real-layout assertions. Authored, not run this sprint.

namespace
{
using namespace Cathode;

class FRecordingLayoutEngine final : public ILayoutEngine
{
public:
	virtual void SetDocument(const ILayoutDocumentSource&) override {}
	virtual void MarkNodeDirty(int32 NodeId, EDirtyKind Kind) override { DirtyMarks.Add(TPair<int32, EDirtyKind>(NodeId, Kind)); }
	virtual FLayoutResult Relayout(int32) override { return FLayoutResult(); }
	TArray<TPair<int32, EDirtyKind>> DirtyMarks;
};

JSValue Eval(JSContext* Context, const char* Source)
{
	return JS_Eval(Context, Source, FCStringAnsi::Strlen(Source), "<cathode-test>", JS_EVAL_TYPE_GLOBAL);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomAttributeTest,
	"Cathode.Dom.Attribute.SetGetRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomAttributeTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	TUniquePtr<ICathodeJsRuntime> RuntimePtr = CreateJsRuntime();
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FRecordingLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);

	FJsValue ElW = Doc->CreateElement(TEXT("input"));
	FDomNode* El = Doc->NodeFromWrapper(ElW);
	const FDomNodeId ElId = El->GetNodeId();

	const int32 Before = Layout.DirtyMarks.Num();

	// set/get round-trip
	El->SetAttribute(TEXT("id"), TEXT("x"));
	FString Out;
	TestTrue(TEXT("getAttribute returns the set value"), El->GetAttribute(TEXT("id"), Out) && Out == FString(TEXT("x")));
	TestFalse(TEXT("getAttribute on a missing attr returns false"), El->GetAttribute(TEXT("missing"), Out));

	// 'value' (get) backed by the value attribute
	El->SetAttribute(TEXT("value"), TEXT("hello"));
	FString Val;
	TestTrue(TEXT("value getter reads the value attribute"), El->GetValue(Val) && Val == FString(TEXT("hello")));

	// remove
	El->RemoveAttribute(TEXT("id"));
	TestFalse(TEXT("getAttribute after remove returns false"), El->GetAttribute(TEXT("id"), Out));

	// dirty-mark: two sets + one remove = 3 marks, each (ElId, Layout). (Conservative attr->Layout.)
	TestEqual(TEXT("set/set/remove fired three dirty-marks"), Layout.DirtyMarks.Num() - Before, 3);
	bool bAllLayout = true;
	for (int32 i = Before; i < Layout.DirtyMarks.Num(); ++i)
	{
		bAllLayout &= (Layout.DirtyMarks[i].Key == ElId && Layout.DirtyMarks[i].Value == EDirtyKind::Layout);
	}
	TestTrue(TEXT("attribute mutations mark (node, Layout)"), bAllLayout);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomTextContentTest,
	"Cathode.Dom.Attribute.TextContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomTextContentTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	TUniquePtr<ICathodeJsRuntime> RuntimePtr = CreateJsRuntime();
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FRecordingLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);

	FJsValue ElW = Doc->CreateElement(TEXT("div"));
	FDomNode* El = Doc->NodeFromWrapper(ElW);
	const FDomNodeId ElId = El->GetNodeId();
	const int32 Before = Layout.DirtyMarks.Num();

	// set textContent -> a single text child; get reads it back
	El->SetTextContent(TEXT("hi"));
	TestEqual(TEXT("textContent get reads the set text"), El->GetTextContent(), FString(TEXT("hi")));
	{
		TArray<FDomNodeId> Kids;
		El->GetChildNodes(Kids);
		TestEqual(TEXT("textContent set leaves one text child"), Kids.Num(), 1);
	}

	// set again REPLACES (not appends)
	El->SetTextContent(TEXT("bye"));
	TestEqual(TEXT("textContent set replaces"), El->GetTextContent(), FString(TEXT("bye")));
	{
		TArray<FDomNodeId> Kids;
		El->GetChildNodes(Kids);
		TestEqual(TEXT("still one text child after replace"), Kids.Num(), 1);
	}

	// each textContent set is a Structure dirty-mark (children replaced)
	TestEqual(TEXT("two textContent sets fired two dirty-marks"), Layout.DirtyMarks.Num() - Before, 2);
	bool bAllStructure = true;
	for (int32 i = Before; i < Layout.DirtyMarks.Num(); ++i)
	{
		bAllStructure &= (Layout.DirtyMarks[i].Key == ElId && Layout.DirtyMarks[i].Value == EDirtyKind::Structure);
	}
	TestTrue(TEXT("textContent set marks (node, Structure)"), bAllStructure);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomMarshalTest,
	"Cathode.Dom.Attribute.MarshalValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomMarshalTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	TUniquePtr<ICathodeJsRuntime> RuntimePtr = CreateJsRuntime();
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	auto MarshalAndFree = [&](JSValue Value, FString& Out) -> bool
	{
		const bool bOk = MarshalAttributeValue(Context, Value, Out);
		JS_FreeValue(Context, Value);
		return bOk;
	};

	FString Out;
	// ACCEPT the scalar vocabulary (our converted values)
	TestTrue(TEXT("string marshals as-is"), MarshalAndFree(JS_NewString(Context, "s"), Out) && Out == FString(TEXT("s")));
	TestTrue(TEXT("bool true marshals to \"true\""), MarshalAndFree(JS_NewBool(Context, true), Out) && Out == FString(TEXT("true")));
	TestTrue(TEXT("bool false marshals to \"false\""), MarshalAndFree(JS_NewBool(Context, false), Out) && Out == FString(TEXT("false")));
	TestTrue(TEXT("number marshals to a non-empty string"), MarshalAndFree(JS_NewFloat64(Context, 42.0), Out) && !Out.IsEmpty());

	// REJECT exotic + non-scalar
	TestFalse(TEXT("record rejected"), MarshalAndFree(JS_NewObject(Context), Out));
	TestFalse(TEXT("array rejected"), MarshalAndFree(JS_NewArray(Context), Out));
	TestFalse(TEXT("symbol rejected"), MarshalAndFree(Eval(Context, "Symbol()"), Out));
	TestFalse(TEXT("null rejected (non-scalar)"), MarshalAndFree(JS_NULL, Out));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
