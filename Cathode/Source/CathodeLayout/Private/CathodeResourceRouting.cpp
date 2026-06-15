#include "CathodeResourceRouting.h"

#include "CathodeRaster.h" // sprint 011 [c]: DecodeImageSize (Blend2D image-info) for get_image_size

namespace Cathode
{

bool RouteImageBytes(const IResourceResolver* Resolver, const FString& Src, const FString& BaseUrl, TArray<uint8>& OutBytes)
{
	// load_image: resolve the image bytes (available for the 003 draw at integration). Null resolver -> miss.
	return Resolver != nullptr && Resolver->ResolveBytes(Src, BaseUrl, OutBytes);
}

bool RouteLinkedCss(const IResourceResolver* Resolver, const FString& Url, const FString& BaseUrl, FString& OutCss)
{
	// import_css: resolve the linked stylesheet text (the container hands it to litehtml to parse). Null -> miss.
	return Resolver != nullptr && Resolver->ResolveText(Url, BaseUrl, OutCss);
}

void RouteImageSize(const IResourceResolver* Resolver, const FString& Src, const FString& BaseUrl, int32& OutWidth, int32& OutHeight)
{
	// get_image_size: route to the resolver for the bytes, then DECODE their intrinsic dimensions via the 003
	// Blend2D image-info decode (DecodeImageSize). A null resolver / miss / undecodable bytes -> 0x0.
	OutWidth = 0;
	OutHeight = 0;
	TArray<uint8> Bytes;
	if (Resolver != nullptr && Resolver->ResolveBytes(Src, BaseUrl, Bytes) && Bytes.Num() > 0)
	{
		DecodeImageSize(Bytes.GetData(), Bytes.Num(), OutWidth, OutHeight);
	}
}

} // namespace Cathode
