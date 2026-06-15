#if WITH_DEV_AUTOMATION_TESTS

#include "CathodeDom.h"
#include "CathodeJsRuntime.h"
#include "CathodeRuntimeTesting.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

// Sprint 012 — THE FIRST-MOUNT PROOF: a real (small, static) Preact panel mounts + renders END-TO-END on Cathode
// through the assembled 012 surface (createElementNS -> node core + reflected props -> insertBefore -> the DOM),
// asserted via the PRODUCTION document (globalThis.document, set by InstallAsGlobal-direct, PATH a). would-it-
// pass-if-test-only = NO: the document is the production InstallAsGlobal, the DOM members are the production
// bindings, and the render is REAL Preact (not a hand-built DOM).
//
// FORWARD NOTE (carry-not-rediscover): the load-time SHIMS below (globalThis.process = {} + globalThis.self =
// globalThis) are required to load the Preact UMD bundle into the BARE runtime (Preact core sets
// process._rerenderCount at module load; the UMD detector reads self). PRODUCTION Preact-loading — the forward
// LoadAuthorScript / surface-load flow (docs/browse/surface-load-document-wiring.md) — will need the SAME
// runtime-environment shims. This is a FORWARD note, not this slice's wiring.

namespace
{
using namespace Cathode;

class FStubLayoutEngine final : public ILayoutEngine
{
public:
	virtual void SetDocument(const ILayoutDocumentSource&) override {}
	virtual void MarkNodeDirty(int32, EDirtyKind) override {}
	virtual FLayoutResult Relayout(int32) override { return FLayoutResult(); }
};

// Eval a source string at global scope; on an uncaught exception surface the message + return false.
bool EvalSource(JSContext* Ctx, const FString& Source, const char* Name, FString& OutError)
{
	const FTCHARToUTF8 Utf8(*Source);
	JSValue Result = JS_Eval(Ctx, Utf8.Get(), Utf8.Length(), Name, JS_EVAL_TYPE_GLOBAL);
	bool bOk = true;
	if (JS_IsException(Result))
	{
		bOk = false;
		JSValue Exc = JS_GetException(Ctx);
		const char* Msg = JS_ToCString(Ctx, Exc);
		OutError = Msg ? FString(UTF8_TO_TCHAR(Msg)) : TEXT("<unknown JS exception>");
		if (Msg) { JS_FreeCString(Ctx, Msg); }
		JS_FreeValue(Ctx, Exc);
	}
	JS_FreeValue(Ctx, Result);
	return bOk;
}

bool EvalBool(JSContext* Ctx, const char* Src)
{
	JSValue Result = JS_Eval(Ctx, Src, FCStringAnsi::Strlen(Src), "<mount-oracle>", JS_EVAL_TYPE_GLOBAL);
	bool bValue = false;
	const bool bOk = UnmarshalBool(Ctx, Result, bValue);
	JS_FreeValue(Ctx, Result);
	return bOk && bValue;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCathodeDomFirstMountProofTest,
	"Cathode.Dom.Mount.FirstMountProof",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCathodeDomFirstMountProofTest::RunTest(const FString& Parameters)
{
	using namespace Cathode;

	Cathode::FScopedTestRuntime RuntimePtr;
	if (!RuntimePtr.IsValid()) { AddError(TEXT("Cathode test runtime failed to initialize")); return false; }
	TUniquePtr<INativeClassRegistry> Registry = CreateNativeClassRegistry(*RuntimePtr);
	FStubLayoutEngine Layout;
	TUniquePtr<FDomDocument> Doc = CreateDomDocument(*RuntimePtr, *Registry, Layout);
	JSContext* Context = static_cast<JSContext*>(RuntimePtr->GetNativeContextHandle());

	// PATH (a): the document is exposed via the PRODUCTION install method on the live context.
	Doc->InstallAsGlobal();

	// Locate the vendored Preact bundle + the panel fixture (plugin-reachable on the build box — panels/ is not).
	// NB: the Preact-sourcing (how/where the bundle is obtained) is browse-design-ruled; this path finalizes with
	// that ruling.
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Cathode"));
	if (!Plugin.IsValid()) { AddError(TEXT("Cathode plugin not found")); return false; }
	// TEST-FIXTURE JS (test-only; distinct from production author-JS): the genuine upstream Preact 10.29.2 UMD
	// bundle (provenance: PROVENANCE.md — fetched from npm upstream, NOT panels/) + the Cathode-owned panel.
	const FString JsDir = Plugin->GetBaseDir() / TEXT("Source/CathodeDom/Private/Tests/JS");
	FString PreactSource, PanelSource;
	if (!FFileHelper::LoadFileToString(PreactSource, *(JsDir / TEXT("preact.umd.js"))))
	{
		AddError(TEXT("could not load the vendored Preact bundle (Source/CathodeDom/JS/preact.umd.js)"));
		return false;
	}
	if (!FFileHelper::LoadFileToString(PanelSource, *(JsDir / TEXT("mount-proof-panel.js"))))
	{
		AddError(TEXT("could not load the panel fixture (mount-proof-panel.js)"));
		return false;
	}

	// Bare-runtime SHIMS, then Preact, then the panel fixture.
	FString EvalError;
	if (!EvalSource(Context, TEXT("globalThis.process = {}; globalThis.self = globalThis;"), "<shims>", EvalError))
	{
		AddError(FString::Printf(TEXT("shims threw: %s"), *EvalError));
		return false;
	}
	if (!EvalSource(Context, PreactSource, "preact.umd.js", EvalError))
	{
		AddError(FString::Printf(TEXT("Preact bundle threw on load: %s"), *EvalError));
		return false;
	}
	if (!EvalBool(Context, "typeof globalThis.preact === 'object' && typeof globalThis.preact.h === 'function' && typeof globalThis.preact.render === 'function'"))
	{
		AddError(TEXT("Preact did not self-register globalThis.preact (h/render)"));
		return false;
	}
	if (!EvalSource(Context, PanelSource, "mount-proof-panel.js", EvalError))
	{
		AddError(FString::Printf(TEXT("panel fixture threw on load: %s"), *EvalError));
		return false;
	}

	// A container element under the document tree (documentElement/body are deferred, so wire it C++-side).
	FJsValue RootW = Doc->GetWrapperForNode(Doc->GetRootNodeId());
	FDomNode* Root = Doc->NodeFromWrapper(RootW);
	FJsValue ContainerW = Doc->CreateElement(TEXT("div"));
	if (Root == nullptr) { AddError(TEXT("document root did not resolve")); return false; }
	Root->AppendChild(ContainerW);
	JSValue Global = JS_GetGlobalObject(Context);
	JS_SetPropertyStr(Context, Global, "__container", JS_DupValue(Context, ContainerW.GetValue()));
	JS_FreeValue(Context, Global);

	// THE REAL MOUNT: Preact renders the panel into the container through the assembled DOM surface.
	if (!EvalSource(Context, TEXT("__mountPanel(__container);"), "<mount>", EvalError))
	{
		AddError(FString::Printf(TEXT("Preact render threw during mount: %s"), *EvalError));
		return false;
	}

	// MOUNT-SUCCESS ORACLE — the REAL rendered DOM, read END-TO-END through globalThis.document.
	TestTrue(TEXT("mounted #app: a div with the reflected className + the setAttribute attr, nodeType 1"),
		EvalBool(Context, "(function(){var e=document.getElementById('app');return e!==null && e.tagName==='div' && e.className==='panel' && e.getAttribute('data-kind')==='demo' && e.nodeType===1;})()"));
	TestTrue(TEXT("mounted #title: h1.heading with text 'Cathode'"),
		EvalBool(Context, "(function(){var e=document.getElementById('title');return e!==null && e.tagName==='h1' && e.className==='heading' && e.textContent==='Cathode';})()"));
	TestTrue(TEXT("mounted #msg: p.body with text 'Preact mounted'"),
		EvalBool(Context, "(function(){var e=document.getElementById('msg');return e!==null && e.tagName==='p' && e.className==='body' && e.textContent==='Preact mounted';})()"));
	TestTrue(TEXT("structure: #app has 2 children, firstChild === #title (insertBefore order)"),
		EvalBool(Context, "(function(){var a=document.getElementById('app');return a.childNodes.length===2 && a.firstChild===document.getElementById('title');})()"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
