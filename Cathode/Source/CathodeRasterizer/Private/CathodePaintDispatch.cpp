#include "CathodePaintDispatch.h"

namespace Cathode
{

void DispatchPaintOps(const TArray<FPaintOp>& Ops, IRasterContext& Context, const FRasterConfig& Config)
{
	for (const FPaintOp& Op : Ops)
	{
		switch (Op.Kind)
		{
		case EPaintOpKind::FillRect:
			Context.FillRect(Op.Rect, Op.Color);
			break;

		case EPaintOpKind::FillBox:
			Context.FillBox(Op.Rect, Op.Color);
			break;

		case EPaintOpKind::DrawImage:
			if (Op.Image != nullptr)
			{
				Context.DrawImage(Op.Rect, *Op.Image);
			}
			break;

		case EPaintOpKind::DrawGlyphRun:
		{
			// Fractional glyph positioning (decided default): pass the origin UNSNAPPED. Only round to the
			// pixel grid if fractional positioning is explicitly disabled.
			const FVector2f Origin = Config.bFractionalGlyphPositioning
				? Op.GlyphOrigin
				: FVector2f(FMath::RoundToFloat(Op.GlyphOrigin.X), FMath::RoundToFloat(Op.GlyphOrigin.Y));
			Context.DrawGlyphRun(Origin, Op.GlyphRun, Op.Color);
			break;
		}

		case EPaintOpKind::ClearRect:
			Context.ClearRect(Op.Rect);
			break;
		}
	}
}

} // namespace Cathode
