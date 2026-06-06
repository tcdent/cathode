#include "CathodeShapeFeatures.h"

namespace Cathode
{

TArray<FShapeFeature> BuildShapeFeatures(const FTextFeatures& Features)
{
	TArray<FShapeFeature> Out;
	Out.Add(FShapeFeature{ MakeOpenTypeTag('k', 'e', 'r', 'n'), Features.bKerning ? 1u : 0u });
	Out.Add(FShapeFeature{ MakeOpenTypeTag('l', 'i', 'g', 'a'), Features.bLigatures ? 1u : 0u });
	Out.Add(FShapeFeature{ MakeOpenTypeTag('c', 'a', 'l', 't'), Features.bContextualAlternates ? 1u : 0u });
	return Out;
}

} // namespace Cathode
