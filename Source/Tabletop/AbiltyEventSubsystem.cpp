#include "AbiltyEventSubsystem.h"

void UAbilityEventSubsystem::Broadcast(const FAbilityEventContext& Ctx)
{
	{
		OnAny.Broadcast(Ctx);
		OnUnit.Broadcast(Ctx, Ctx.Source);
		OnUnitVsUnit.Broadcast(Ctx, Ctx.Source, Ctx.Target);
	}
}
