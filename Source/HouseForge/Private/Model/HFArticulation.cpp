// Copyright Siddartha G. All Rights Reserved.

#include "Model/HFArticulation.h"

FVector FHFPartMotion::UnitAxis() const
{
	// A zero axis would give a garbage quaternion that looks like a random rotation rather than
	// like an error, so fall back to something harmless and obvious.
	const FVector Normalised = Axis.GetSafeNormal();
	return Normalised.IsNearlyZero() ? FVector::ZAxisVector : Normalised;
}

FTransform FHFPartMotion::OffsetAt(double OpenAmount) const
{
	const double Alpha = FMath::Clamp(OpenAmount, 0.0, 1.0);

	switch (Type)
	{
	case EHFMotionType::Hinge:
		return FTransform(FQuat(UnitAxis(), FMath::DegreesToRadians(MaxAngleDegrees * Alpha)));

	case EHFMotionType::Slide:
		return FTransform(UnitAxis() * (MaxTravelCm * Alpha));

	default:
		// A spin lands here too. It has no open amount, so no amount can move it - only a phase can,
		// through SpinOffsetAt.
		return FTransform::Identity;
	}
}

FTransform FHFPartMotion::SpinOffsetAt(double Turns) const
{
	if (Type != EHFMotionType::Spin)
	{
		return FTransform::Identity;
	}

	// Not clamped and not wrapped. Wrapping into 0..1 would be invisible in a single pose and would
	// quietly destroy the thing this exists for - a phase that keeps counting past a full turn -
	// the moment anything integrated it or interpolated between two of them.
	return FTransform(FQuat(UnitAxis(), Turns * UE_DOUBLE_TWO_PI));
}

double FHFPartMotion::AllowanceFrom(double BlockerOpenAmount) const
{
	if (SequencedAfterPartId.IsNone())
	{
		return 1.0;
	}

	const double Blocker = FMath::Clamp(BlockerOpenAmount, 0.0, 1.0);
	const double Threshold = FMath::Clamp(SequenceThreshold, 0.0, 1.0);

	// A threshold of 1 means "not until it is all the way open", which has no range to ramp over.
	if (Threshold >= 1.0)
	{
		return Blocker >= 1.0 ? 1.0 : 0.0;
	}

	return FMath::Clamp((Blocker - Threshold) / (1.0 - Threshold), 0.0, 1.0);
}

FVector FHFPartMotion::SweptLocalPoint(const FVector& LocalPoint, double OpenAmount) const
{
	return OffsetAt(OpenAmount).TransformPosition(LocalPoint);
}

FTransform FHFPartState::PoseAt(double InOpenAmount) const
{
	// A fan is posed by its phase; everything else by how far open it is.
	const FTransform Offset = Motion.Revolves()
		? Motion.SpinOffsetAt(SpinTurns)
		: Motion.OffsetAt(InOpenAmount);

	// Offset first, in the part's own space, then the pivot placing that space in the actor.
	// UE composes left-to-right: (A * B) applies A and then B.
	return Offset * PivotTransform;
}

// ------------------------------------------------------------------------------- assembly solve

bool FHFArticulation::ResolvePartAmounts(TArrayView<FHFPartState> Parts, TArray<FName>* OutCyclicPartIds)
{
	const int32 Count = Parts.Num();
	if (Count == 0)
	{
		return true;
	}

	TMap<FName, int32> IndexById;
	IndexById.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// First wins, matching every other lookup on the actor. Duplicate ids are refused at
		// generation, so this is belt and braces rather than a policy.
		IndexById.FindOrAdd(Parts[Index].PartId, Index);
	}

	auto DependencyOf = [&Parts, &IndexById](int32 Index, int32 Which) -> int32
	{
		const FHFPartMotion& Motion = Parts[Index].Motion;
		const FName Id = Which == 0 ? Motion.DrivenByPartId : Motion.SequencedAfterPartId;
		if (Id.IsNone())
		{
			return INDEX_NONE;
		}

		// A part naming ITSELF is left in deliberately. It is a cycle of one, and the search below
		// sees it as exactly that - a back edge into a node still on the stack - so it is reported
		// with the rest rather than quietly dropped and then resolved as though it were sound.
		const int32* Found = IndexById.Find(Id);
		return Found != nullptr ? *Found : INDEX_NONE;
	};

	// ------------------------------------------------------------------- find the cycles first
	//
	// Structural detection rather than "give up after N passes". A cycle whose parts happen to
	// already agree converges immediately, so an iteration cap would call it resolved and leave a
	// fixture that mysteriously stops responding as soon as anything moves. Colours: 0 unvisited,
	// 1 on the stack, 2 done. Iterative, because a deep chain of parts must not recurse on the
	// editor's stack.

	TArray<uint8> Colour;
	Colour.Init(0, Count);

	TArray<bool> bOnCycle;
	bOnCycle.Init(false, Count);

	TArray<int32> Stack;
	TArray<int32> NextEdge;

	for (int32 Root = 0; Root < Count; ++Root)
	{
		if (Colour[Root] != 0)
		{
			continue;
		}

		Stack.Reset();
		NextEdge.Reset();
		Stack.Push(Root);
		NextEdge.Push(0);
		Colour[Root] = 1;

		while (!Stack.IsEmpty())
		{
			const int32 Node = Stack.Last();
			const int32 Edge = NextEdge.Last();

			if (Edge >= 2)
			{
				Colour[Node] = 2;
				Stack.Pop();
				NextEdge.Pop();
				continue;
			}

			++NextEdge.Last();

			const int32 Next = DependencyOf(Node, Edge);
			if (Next == INDEX_NONE || Colour[Next] == 2)
			{
				continue;
			}

			if (Colour[Next] == 1)
			{
				// Back edge into something still on the stack: everything from it to the top is on a
				// cycle with it.
				for (int32 Depth = Stack.Num() - 1; Depth >= 0; --Depth)
				{
					bOnCycle[Stack[Depth]] = true;
					if (Stack[Depth] == Next)
					{
						break;
					}
				}
				continue;
			}

			Colour[Next] = 1;
			Stack.Push(Next);
			NextEdge.Push(0);
		}
	}

	// ------------------------------------------------------------------------- settle the rest

	TArray<double> Requested;
	Requested.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Requested.Add(FMath::Clamp(Parts[Index].OpenAmount, 0.0, 1.0));
		Parts[Index].OpenAmount = Requested[Index];
	}

	// Each pass recomputes from the requests rather than from the previous pass's answers, so the
	// iteration converges on the dependencies instead of ratcheting through them. A chain of depth
	// D settles in D passes; the cap is the longest chain possible over this many parts.
	const int32 MaxPasses = Count + 1;

	for (int32 Pass = 0; Pass < MaxPasses; ++Pass)
	{
		bool bChanged = false;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			double Amount = Requested[Index];

			if (!bOnCycle[Index])
			{
				if (const int32 Driver = DependencyOf(Index, 0); Driver != INDEX_NONE)
				{
					// Geared: the driver's amount, whatever this part was asked for. The gearing
					// ratio lives in this part's own travel, not in the amount.
					Amount = Parts[Driver].OpenAmount;
				}

				if (const int32 Blocker = DependencyOf(Index, 1); Blocker != INDEX_NONE)
				{
					// Sequenced: no further than the part in front of it allows.
					Amount = FMath::Min(Amount, Parts[Index].Motion.AllowanceFrom(Parts[Blocker].OpenAmount));
				}
			}

			if (!FMath::IsNearlyEqual(Amount, Parts[Index].OpenAmount, UE_DOUBLE_SMALL_NUMBER))
			{
				Parts[Index].OpenAmount = Amount;
				bChanged = true;
			}
		}

		if (!bChanged)
		{
			break;
		}
	}

	if (OutCyclicPartIds != nullptr)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (bOnCycle[Index])
			{
				OutCyclicPartIds->Add(Parts[Index].PartId);
			}
		}
	}

	return !bOnCycle.Contains(true);
}
