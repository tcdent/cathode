#include "CathodeBinding.h"

// quickjs-ng — confined to this TU. Include path wired at the execution-approval sprint; no build this sprint.
THIRD_PARTY_INCLUDES_START
#include "quickjs.h"
THIRD_PARTY_INCLUDES_END

namespace Cathode
{

namespace
{

// The appendable native-class registry — one generic path consumers add their native classes to. Holds the
// class IDs registered for the runtime; backed by quickjs-ng's own class machinery. Private to this TU; only
// the factory is exposed. Composes 002's ONE confined context (obtained via the opaque accessor), never a
// second runtime.
class FNativeClassRegistry final : public INativeClassRegistry
{
public:
	explicit FNativeClassRegistry(ICathodeJsRuntime& InRuntime)
		: Context(static_cast<JSContext*>(InRuntime.GetNativeContextHandle()))
	{
	}

	virtual JSClassID RegisterClass(const FNativeClassDef& Def) override
	{
		if (Context == nullptr)
		{
			return 0;
		}
		JSRuntime* Runtime = JS_GetRuntime(Context);

		JSClassID ClassId = 0;
		// TODO-PREBUILD(signature): JS_NewClassID is 2-arg (rt, &id) in -ng (quickjs.h:688); original quickjs
		// was 1-arg (&id). Returns the allocated id.
		JS_NewClassID(Runtime, &ClassId);

		JSClassDef ClassDef = {};
		ClassDef.class_name = Def.Name;
		// TODO-PREBUILD(signature): JSClassFinalizer = void(JSRuntime*, JSValueConst) (quickjs.h:654);
		// JSClassGCMark = void(JSRuntime*, JSValueConst, JS_MarkFunc*) (quickjs.h:655). Verify exact typedefs.
		ClassDef.finalizer = Def.Finalizer;
		// gc_mark pass-through ONLY — a null GcMark must behave exactly as today (no cycle-collection policy
		// is baked here; that is the human's Slice 4 call). Non-null wires -ng's native cycle-traversal hook.
		ClassDef.gc_mark = Def.GcMark;

		// TODO-PREBUILD(signature): JS_NewClass(rt, id, &def) -> int (quickjs.h:691).
		JS_NewClass(Runtime, ClassId, &ClassDef);

		RegisteredClasses.Add(ClassId);
		return ClassId;
	}

	virtual FJsValue CreateWrappedObject(JSClassID ClassId, void* Opaque) override
	{
		if (Context == nullptr)
		{
			return FJsValue();
		}

		// The class proto (JS_NULL when none was registered — fine for the generic mechanism; consumers/later
		// slices set a proto via JS_SetClassProto). JS_GetClassProto returns an owned ref we must free.
		JSValue Proto = JS_GetClassProto(Context, ClassId);
		// TODO-PREBUILD(signature): JS_NewObjectProtoClass(ctx, proto, class_id) -> JSValue (quickjs.h:904).
		JSValue Object = JS_NewObjectProtoClass(Context, Proto, ClassId);
		JS_FreeValue(Context, Proto);

		// TODO-PREBUILD(signature): JS_SetOpaque(obj, ptr) returns int + takes JSValueConst in -ng
		// (quickjs.h:1039); original quickjs returned void / took JSValue. Return ignored intentionally.
		JS_SetOpaque(Object, Opaque);

		return FJsValue(Context, Object); // takes ownership of the new object's reference
	}

	virtual bool IsRegistered(JSClassID ClassId) const override
	{
		return RegisteredClasses.Contains(ClassId);
	}

private:
	JSContext* Context;
	TSet<JSClassID> RegisteredClasses;
};

} // namespace

TUniquePtr<INativeClassRegistry> CreateNativeClassRegistry(ICathodeJsRuntime& Runtime)
{
	return MakeUnique<FNativeClassRegistry>(Runtime);
}

// --- FNativeObjectHandle (the wrapped-native-object handle) ---

FNativeObjectHandle::FNativeObjectHandle(FJsValue InObject)
	: WrappedObject(MoveTemp(InObject))
{
}

bool FNativeObjectHandle::IsValid() const
{
	return WrappedObject.IsValid();
}

void* FNativeObjectHandle::GetOpaque(JSClassID ExpectedClass) const
{
	if (!WrappedObject.IsValid())
	{
		return nullptr;
	}
	// TODO-PREBUILD(signature): JS_GetOpaque(obj, class_id) -> void* (quickjs.h:1040); JSValueConst arg in -ng.
	return JS_GetOpaque(WrappedObject.GetValue(), ExpectedClass);
}

} // namespace Cathode
