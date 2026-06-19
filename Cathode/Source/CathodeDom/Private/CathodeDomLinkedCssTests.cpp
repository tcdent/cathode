#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeResolver.h"
#include "CathodeRuntimeTesting.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// 020 slice 1 (A1) — litehtml on the REAL RESOLVER: a LINKED-CSS round-trip through the REAL FUEFileBundleSource.
// The genuine gap (the 019 deferred wiring): SetResourceResolver has NO production caller; nothing proves a real
// EXTERNAL resource resolves THROUGH the real resolver into litehtml. This test wires the real resolver
// (CreateResourceResolver(CreateUEFileBundleSource(<test fixtures dir>))) onto a real FDomDocument's litehtml engine,
// and proves a `<style>@import url('linked.css')` GENUINELY drives litehtml's import_css -> RouteLinkedCss -> the real
// FFileHelper disk read -> litehtml cascades the linked rule -> getComputedStyle reflects the LINKED value.
//
// THE TRIGGER IS GENUINE (not a C++ hand-call -- that would be the existing RouteLinkedCss routing coverage): litehtml's
// OWN parse of the serialized document (createFromString over Source.GetDocumentHtml()) fires import_css. The @import
// reaches litehtml via the <style> raw-text serialization (GetDocumentHtml; GetDocumentCss is empty -> NOT used). NO
// production change (no GetDocumentCss impl, no <link>, no production SetResourceResolver caller).
//
// THE NULL-RESOLVER CONTROL (the discriminator): with null, RouteLinkedCss returns false -> import_css leaves the CSS
// empty -> the linked rule never loads -> the element gets the DEFAULT. The pair (real -> the linked value; null -> the
// default, != linked) proves the linked value came THROUGH the real resolver. The linked color is DISTINCT + NON-echoable
// (it's in the EXTERNAL committed fixture, not any inline style).

namespace
{
using namespace Cathode;

class FStubMeasurer final : public ITextMeasurer
{
public:
	virtual FLayoutFontRef ResolveFont(const FString&, int32, int32, bool) override { return FLayoutFontRef(); }
	virtual float MeasureWidth(const FString&, FLayoutFontRef) override { return 0.0f; }
	virtual void GetFontMetrics(FLayoutFontRef, float& A, float& D, float& H) override { A = 12.0f; D = 4.0f; H = 16.0f; }
};

// The test fixtures dir (the 019 GetBaseDir convention); the committed linked.css lives here.
FString FixturesRootDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	return Plugin.IsValid() ? (Plugin->GetBaseDir() / TEXT("Source/CathodeDom/Private/Tests/Fixtures")) : FString();
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomLinkedCssTest,
	"Cathode.Dom.Resolver.LinkedCssRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomLinkedCssTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	FScopedTestRuntime Runtime;
	if (!Runtime.IsValid()) { AddError(TEXT("runtime failed to start")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*Runtime);
	FStubMeasurer Measurer;

	const FString RootDir = FixturesRootDir();
	if (RootDir.IsEmpty()) { AddError(TEXT("could not locate the Cathode plugin base dir (IPluginManager)")); return false; }
	// Precondition: the committed linked-CSS fixture is on disk at run time (the GetBaseDir convention).
	const FString FixtureFullPath = FPaths::Combine(RootDir, FString(TEXT("linked.css")));
	if (!FPaths::FileExists(FixtureFullPath))
	{
		AddError(FString::Printf(TEXT("the committed linked-CSS fixture is not on disk: %s"), *FixtureFullPath));
		return false;
	}

	// The REAL resolver over the REAL FUEFileBundleSource (the 019 FFileHelper disk read) — outlives the per-render engines.
	TUniquePtr<IBundleSource> BundleSource = CreateUEFileBundleSource(RootDir);
	TUniquePtr<IResourceResolver> Resolver = CreateResourceResolver(*BundleSource);

	JSContext* Context = static_cast<JSContext*>(Runtime->GetNativeContextHandle());

	// Build a real FDomDocument with `<style>@import url('linked.css')` + a #target div, bind the given resolver onto its
	// litehtml engine, Relayout (litehtml parses the <style>@import -> import_css -> the resolver -> cascade), and read the
	// real getComputedStyle(#target).color. Returns the computed color string.
	auto RenderTargetColor = [&](IResourceResolver* ResolverPtr) -> FString
	{
		TUniquePtr<ILayoutEngine> Engine = CreateLiteHtmlLayoutEngine(Measurer);
		Engine->SetResourceResolver(ResolverPtr); // BEFORE CreateDomDocument (which self-wires Layout.SetDocument(*this))
		TUniquePtr<FDomDocument> Doc = CreateDomDocument(*Runtime, *Registry, *Engine);

		FDomNode* Html = Doc->FindNode(Doc->GetRootNodeId());
		FJsValue StyleW = Doc->CreateElement(TEXT("style"));
		Doc->NodeFromWrapper(StyleW)->SetTextContent(TEXT("@import url('linked.css');")); // raw-text serialized -> litehtml import_css
		Html->AppendChild(StyleW);
		FJsValue TargetW = Doc->CreateElement(TEXT("div"));
		Doc->NodeFromWrapper(TargetW)->SetAttribute(TEXT("id"), TEXT("cathode-020-target")); // matched by the external rule
		Html->AppendChild(TargetW);

		JSValue Global = JS_GetGlobalObject(Context);
		JS_SetPropertyStr(Context, Global, "__target", JS_DupValue(Context, TargetW.GetValue()));
		JS_FreeValue(Context, Global);

		Doc->Relayout(800); // the real litehtml cascade (drives import_css via the <style>@import)

		const char* Expr = "getComputedStyle(__target)['color']";
		JSValue R = JS_Eval(Context, Expr, FCStringAnsi::Strlen(Expr), "<linked-css-gcs>", JS_EVAL_TYPE_GLOBAL);
		FString Color;
		if (!JS_IsException(R))
		{
			const char* C = JS_ToCString(Context, R);
			Color = FString(UTF8_TO_TCHAR(C));
			JS_FreeCString(Context, C);
		}
		JS_FreeValue(Context, R);
		return Color;
	};

	// The LINKED value (the external fixture sets #cathode-020-target { color:#ff00ff } -> getComputedStyle comma-form).
	// DISTINCT + NON-echoable (no inline color anywhere; it lives only in the committed fixture).
	const FString Linked = TEXT("rgb(255,0,255)");
	const FString RealColor = RenderTargetColor(Resolver.Get());
	const FString NullColor = RenderTargetColor(nullptr);

	// (real) the linked @import loaded THROUGH the real FUEFileBundleSource -> getComputedStyle == the LINKED value.
	TestEqual(FString::Printf(TEXT("(real resolver) the linked @import CSS loaded via the real FUEFileBundleSource -> getComputedStyle.color == the LINKED value (got '%s')"), *RealColor),
		RealColor, Linked);
	// (null, the control) no resolver -> import_css empty -> the linked rule never loads -> the DEFAULT, != the linked value.
	TestTrue(FString::Printf(TEXT("(null resolver) the @import did NOT load (RouteLinkedCss false) -> color is the DEFAULT, != the linked value (got '%s')"), *NullColor),
		NullColor != Linked && !NullColor.IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
