// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFCoplanarScan.h"

#include "HouseForge.h"

#include "Algo/Reverse.h"
#include "VectorUtil.h"

using namespace UE::Geometry;

namespace
{
	/** One triangle, already in world space, with everything the comparison needs precomputed. */
	struct FScanTri
	{
		int32 Surface = 0;
		FVector3d V[3];
		FVector3d Normal = FVector3d::UnitZ();
		double Area = 0.0;
		FAxisAlignedBox3d Bounds = FAxisAlignedBox3d::Empty();
	};

	/**
	 * Contribution below which a single triangle-pair overlap is arithmetic, not geometry.
	 *
	 * Two solids meeting along an edge leave slivers of a few hundredths of a square millimetre
	 * wherever their triangulations disagree about where that edge is. Dropped here rather than in
	 * the reported total, so a thousand of them cannot add up to a defect.
	 */
	constexpr double SliverAreaCm2 = 0.01;

	/** Any unit vector perpendicular to N. */
	FVector3d PerpendicularTo(const FVector3d& N)
	{
		const FVector3d Helper = (FMath::Abs(N.Z) < 0.9) ? FVector3d::UnitZ() : FVector3d::UnitX();
		return Helper.Cross(N).GetSafeNormal();
	}

	/** Twice the signed area of a polygon; positive when wound counter-clockwise. */
	double TwiceSignedArea(const TArray<FVector2d>& Poly)
	{
		double Twice = 0.0;
		for (int32 I = 0; I < Poly.Num(); ++I)
		{
			const FVector2d& P = Poly[I];
			const FVector2d& Q = Poly[(I + 1) % Poly.Num()];
			Twice += P.X * Q.Y - Q.X * P.Y;
		}
		return Twice;
	}

	/**
	 * Area of the intersection of two convex polygons, by clipping the first against the second.
	 *
	 * Both are re-wound counter-clockwise on the way in rather than assumed to arrive that way.
	 * Which way a projected triangle winds depends on the handedness of the basis it was projected
	 * into, and Unreal's is left-handed - VectorUtil::Normal reverses its cross product for exactly
	 * that reason. Deriving the clipper's inside test from the winding instead of from a convention
	 * is what keeps this from silently returning zero for every overlap in the flat.
	 */
	double ConvexOverlapArea(const TArray<FVector2d>& InSubject, const TArray<FVector2d>& InClip,
		FVector2d& OutCentroid)
	{
		TArray<FVector2d> Poly = InSubject;
		TArray<FVector2d> Clip = InClip;

		if (TwiceSignedArea(Poly) < 0.0)
		{
			Algo::Reverse(Poly);
		}
		if (TwiceSignedArea(Clip) < 0.0)
		{
			Algo::Reverse(Clip);
		}

		for (int32 Edge = 0; Edge < Clip.Num() && Poly.Num() > 0; ++Edge)
		{
			const FVector2d A = Clip[Edge];
			const FVector2d B = Clip[(Edge + 1) % Clip.Num()];
			const FVector2d Dir = B - A;

			auto Inside = [&A, &Dir](const FVector2d& P)
			{
				return Dir.X * (P.Y - A.Y) - Dir.Y * (P.X - A.X) >= 0.0;
			};

			TArray<FVector2d> Clipped;
			Clipped.Reserve(Poly.Num() + 1);

			for (int32 I = 0; I < Poly.Num(); ++I)
			{
				const FVector2d Current = Poly[I];
				const FVector2d Next = Poly[(I + 1) % Poly.Num()];
				const bool bCurrentIn = Inside(Current);
				const bool bNextIn = Inside(Next);

				if (bCurrentIn)
				{
					Clipped.Add(Current);
				}

				if (bCurrentIn != bNextIn)
				{
					const FVector2d Segment = Next - Current;
					const double Denominator = Dir.X * Segment.Y - Dir.Y * Segment.X;
					if (FMath::Abs(Denominator) > UE_DOUBLE_SMALL_NUMBER)
					{
						const double T = (Dir.X * (Current.Y - A.Y) - Dir.Y * (Current.X - A.X)) / -Denominator;
						Clipped.Add(Current + Segment * T);
					}
				}
			}

			Poly = MoveTemp(Clipped);
		}

		if (Poly.Num() < 3)
		{
			return 0.0;
		}

		double Twice = 0.0;
		FVector2d Sum = FVector2d::Zero();

		for (int32 I = 0; I < Poly.Num(); ++I)
		{
			const FVector2d& P = Poly[I];
			const FVector2d& Q = Poly[(I + 1) % Poly.Num()];
			Twice += P.X * Q.Y - Q.X * P.Y;
			Sum += P;
		}

		OutCentroid = Sum / static_cast<double>(Poly.Num());
		return FMath::Abs(Twice) * 0.5;
	}

	/** Key for a surface pair, order-independent. */
	uint64 PairKey(int32 A, int32 B)
	{
		const uint64 Low = static_cast<uint64>(FMath::Min(A, B));
		const uint64 High = static_cast<uint64>(FMath::Max(A, B));
		return (High << 32) | Low;
	}
}

TArray<FHFCoplanarOverlap> FHFCoplanarScan::Find(TArrayView<const FHFScanSurface> Surfaces,
	const FHFCoplanarScanParams& Params)
{
	TArray<FHFCoplanarOverlap> Result;

	const double CellSize = FMath::Max(Params.BroadPhaseCellCm, 1.0);
	const double CosLimit = FMath::Cos(FMath::DegreesToRadians(FMath::Max(Params.NormalToleranceDegrees, 0.0)));

	// ------------------------------------------------------------------ flatten to world triangles
	TArray<FScanTri> Tris;

	for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); ++SurfaceIndex)
	{
		const FHFScanSurface& Surface = Surfaces[SurfaceIndex];
		if (Surface.Mesh == nullptr)
		{
			continue;
		}

		for (const int32 Tid : Surface.Mesh->TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Surface.Mesh->GetTriVertices(Tid, A, B, C);

			FScanTri Tri;
			Tri.Surface = SurfaceIndex;
			Tri.V[0] = Surface.ToWorld.TransformPosition(A);
			Tri.V[1] = Surface.ToWorld.TransformPosition(B);
			Tri.V[2] = Surface.ToWorld.TransformPosition(C);

			// Winding, not the normal overlay. Winding is what culls, so winding is what decides
			// which way a face points - and it needs no re-deriving through the transform.
			//
			// Taken from VectorUtil::Normal, the engine's own function, rather than from a cross
			// product written out here. Unreal is left-handed and VectorUtil::Normal reverses its
			// cross product to suit; a textbook (b-a)x(c-a) comes out EXACTLY OPPOSED to it on
			// every triangle in this plugin, and a second convention is how a scan for co-facing
			// pairs quietly becomes a scan for opposed ones.
			const double Length = (Tri.V[1] - Tri.V[0]).Cross(Tri.V[2] - Tri.V[0]).Length();
			if (Length <= UE_DOUBLE_SMALL_NUMBER)
			{
				continue;
			}

			Tri.Normal = VectorUtil::Normal(Tri.V[0], Tri.V[1], Tri.V[2]);
			Tri.Area = Length * 0.5;
			if (Tri.Area < SliverAreaCm2)
			{
				continue;
			}

			Tri.Bounds = FAxisAlignedBox3d(Tri.V[0], Tri.V[0]);
			Tri.Bounds.Contain(Tri.V[1]);
			Tri.Bounds.Contain(Tri.V[2]);

			Tris.Add(MoveTemp(Tri));
		}
	}

	if (Tris.Num() < 2)
	{
		return Result;
	}

	// ------------------------------------------------------------------------------- broad phase
	//
	// Coplanar faces are by definition coincident in space, so bucketing by world cell reduces an
	// all-pairs comparison over a whole flat - which is tens of billions of pairs - to a local one.
	// The grid affects only how long this takes, never what it finds.
	TMap<FIntVector, TArray<int32>> Grid;

	auto CellRange = [CellSize](const FAxisAlignedBox3d& Bounds, FIntVector& OutMin, FIntVector& OutMax)
	{
		OutMin = FIntVector(
			FMath::FloorToInt32(Bounds.Min.X / CellSize),
			FMath::FloorToInt32(Bounds.Min.Y / CellSize),
			FMath::FloorToInt32(Bounds.Min.Z / CellSize));
		OutMax = FIntVector(
			FMath::FloorToInt32(Bounds.Max.X / CellSize),
			FMath::FloorToInt32(Bounds.Max.Y / CellSize),
			FMath::FloorToInt32(Bounds.Max.Z / CellSize));
	};

	for (int32 Index = 0; Index < Tris.Num(); ++Index)
	{
		FIntVector Min, Max;
		CellRange(Tris[Index].Bounds, Min, Max);

		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
				{
					Grid.FindOrAdd(FIntVector(X, Y, Z)).Add(Index);
				}
			}
		}
	}

	// ---------------------------------------------------------------------------- narrow phase
	struct FPairTotal
	{
		double AreaCm2 = 0.0;
		double SeparationCm = 0.0;
		double BestPatchCm2 = 0.0;
		FVector3d Sample = FVector3d::Zero();
		FVector3d Normal = FVector3d::UnitZ();
	};

	TMap<uint64, FPairTotal> Totals;
	TSet<int32> Candidates;

	for (int32 I = 0; I < Tris.Num(); ++I)
	{
		const FScanTri& A = Tris[I];

		Candidates.Reset();
		FIntVector Min, Max;
		CellRange(A.Bounds, Min, Max);

		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
				{
					if (const TArray<int32>* Bucket = Grid.Find(FIntVector(X, Y, Z)))
					{
						for (const int32 J : *Bucket)
						{
							if (J > I && Tris[J].Surface != A.Surface)
							{
								Candidates.Add(J);
							}
						}
					}
				}
			}
		}

		for (const int32 J : Candidates)
		{
			const FScanTri& B = Tris[J];

			// Same way, not merely parallel. Opposed faces are a butt joint and cannot flash.
			if (A.Normal.Dot(B.Normal) < CosLimit)
			{
				continue;
			}

			const FVector3d Normal = (A.Normal + B.Normal).GetSafeNormal();
			const double Plane = Normal.Dot(A.V[0]);

			double Separation = 0.0;
			bool bCoplanar = true;

			for (int32 K = 0; K < 3 && bCoplanar; ++K)
			{
				Separation = FMath::Max(Separation, FMath::Abs(Normal.Dot(A.V[K]) - Plane));
				Separation = FMath::Max(Separation, FMath::Abs(Normal.Dot(B.V[K]) - Plane));
				bCoplanar = Separation <= Params.PlaneToleranceCm;
			}

			if (!bCoplanar)
			{
				continue;
			}

			// Any orthonormal basis in the shared plane will do; the clipper re-winds what it is
			// handed, so this does not have to agree with anybody about handedness.
			const FVector3d U = PerpendicularTo(Normal);
			const FVector3d V = Normal.Cross(U);

			TArray<FVector2d> PolyA, PolyB;
			PolyA.Reserve(3);
			PolyB.Reserve(3);
			for (int32 K = 0; K < 3; ++K)
			{
				PolyA.Add(FVector2d(U.Dot(A.V[K]), V.Dot(A.V[K])));
				PolyB.Add(FVector2d(U.Dot(B.V[K]), V.Dot(B.V[K])));
			}

			FVector2d Centroid = FVector2d::Zero();
			const double Overlap = ConvexOverlapArea(PolyA, PolyB, Centroid);
			if (Overlap < SliverAreaCm2)
			{
				continue;
			}

			FPairTotal& Total = Totals.FindOrAdd(PairKey(A.Surface, B.Surface));
			Total.AreaCm2 += Overlap;
			Total.SeparationCm = FMath::Max(Total.SeparationCm, Separation);

			if (Overlap > Total.BestPatchCm2)
			{
				Total.BestPatchCm2 = Overlap;
				Total.Normal = Normal;
				Total.Sample = U * Centroid.X + V * Centroid.Y + Normal * Plane;
			}
		}
	}

	for (const TPair<uint64, FPairTotal>& Entry : Totals)
	{
		if (Entry.Value.AreaCm2 < Params.MinAreaCm2)
		{
			continue;
		}

		const int32 IndexA = static_cast<int32>(Entry.Key & 0xFFFFFFFFull);
		const int32 IndexB = static_cast<int32>(Entry.Key >> 32);

		FHFCoplanarOverlap Overlap;
		Overlap.NameA = Surfaces[IndexA].Name;
		Overlap.NameB = Surfaces[IndexB].Name;
		Overlap.Normal = Entry.Value.Normal;
		Overlap.AreaCm2 = Entry.Value.AreaCm2;
		Overlap.SeparationCm = Entry.Value.SeparationCm;
		Overlap.Sample = Entry.Value.Sample;
		Result.Add(MoveTemp(Overlap));
	}

	Result.Sort([](const FHFCoplanarOverlap& A, const FHFCoplanarOverlap& B)
	{
		return A.AreaCm2 > B.AreaCm2;
	});

	return Result;
}

double FHFCoplanarScan::TotalAreaCm2(TArrayView<const FHFCoplanarOverlap> Overlaps)
{
	double Total = 0.0;
	for (const FHFCoplanarOverlap& Overlap : Overlaps)
	{
		Total += Overlap.AreaCm2;
	}
	return Total;
}

TArray<FString> FHFCoplanarScan::Describe(TArrayView<const FHFCoplanarOverlap> Overlaps, int32 MaxLines)
{
	TArray<FString> Lines;

	const int32 Shown = FMath::Min(Overlaps.Num(), FMath::Max(MaxLines, 0));
	for (int32 Index = 0; Index < Shown; ++Index)
	{
		const FHFCoplanarOverlap& Overlap = Overlaps[Index];
		Lines.Add(FString::Printf(
			TEXT("%s vs %s: %.1f cm2 co-facing, gap %.4f cm, normal (%.2f %.2f %.2f) at (%.1f %.1f %.1f)"),
			*Overlap.NameA, *Overlap.NameB, Overlap.AreaCm2, Overlap.SeparationCm,
			Overlap.Normal.X, Overlap.Normal.Y, Overlap.Normal.Z,
			Overlap.Sample.X, Overlap.Sample.Y, Overlap.Sample.Z));
	}

	if (Overlaps.Num() > Shown)
	{
		Lines.Add(FString::Printf(TEXT("... and %d more pairs"), Overlaps.Num() - Shown));
	}

	return Lines;
}
