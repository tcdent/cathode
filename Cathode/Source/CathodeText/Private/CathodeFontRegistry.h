#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

#include "CathodeText.h" // IFontRegistry, FFontHandle

namespace Cathode
{

// The font-file byte loader seam (injectable). Production loads from the local file system / UE file
// APIs; tests inject a recording loader so the registry's bookkeeping is testable WITHOUT touching real
// file I/O. Returns true + the bytes on success. The bytes are opaque to the registry.
using FFontFileLoader = TFunction<bool(const FString& Path, TArray<uint8>& OutBytes)>;

// The font registry + single-load byte store. Loads each font file ONCE (via the injected loader), owns
// the byte lifetime, and resolves an engine-neutral FFontHandle to a stable const bytes view. The
// inner byte arrays are heap-stable (TUniquePtr) so a handle's bytes view never moves as more fonts
// register — the same-handle -> same-bytes identity the same-face guarantee rests on.
class FFontRegistry final : public IFontRegistry
{
public:
	explicit FFontRegistry(FFontFileLoader InLoader);

	// IFontRegistry
	virtual FFontHandle RegisterFontFile(const FString& Path) override;

	// IFontByteStore
	virtual bool GetFontBytes(FFontHandle Handle, TArrayView<const uint8>& OutBytes) const override;

private:
	FFontFileLoader Loader;
	TArray<TUniquePtr<TArray<uint8>>> FontBytes; // index = handle id; owns each font's bytes (heap-stable)
	TMap<FString, int32> PathToHandle;           // same path -> same handle (no reload)
};

} // namespace Cathode
