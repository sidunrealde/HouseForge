// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFClashScan.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Spatial/FastWinding.h"

using namespace UE::Geometry;

namespace HouseForgeClash
{
	/**
	 * A surface with everything the scan needs to answer "is this point inside you, and how deep".
	 *
	 * Trees are built LAZILY. A flat is a couple of hundred meshes and most pairs never meet, so
	 * building a winding tree for every one of them costs more than the whole scan.
	 */
	struct FSolid
	{
		const FHFScanSurface* Surface = nullptr;

		/** World-space bounds, off the vertices - not a component's padded render bounds. */
		FAxisAlignedBox3d WorldBounds = FAxisAlignedBox3d::Empty();

		/** The mesh's own bounds in its own frame, for the cheap reject before a winding query. */
		FAxisAlignedBox3d LocalBounds = FAxisAlignedBox3d::Empty();

		TUniquePtr<FDynamicMeshAABBTree3> Tree;
		TUniquePtr<TFastWindingTree<FDynamicMesh3>> Winding;

		void Build()
		{
			if (!Tree.IsValid())
			{
				Tree = MakeUnique<FDynamicMeshAABBTree3>(Surface->Mesh, true);
				Winding = MakeUnique<TFastWindingTree<FDynamicMesh3>>(Tree.Get());
			}
		}

		/** True when a world point is inside this solid. Cheap reject first. */
		bool Contains(const FVector3d& World)
		{
			const FVector3d Local = Surface->ToWorld.InverseTransformPosition(World);
			if (!LocalBounds.Contains(Local))
			{
				return false;
			}

			Build();
			return Winding->FastWindingNumber(Local) > 0.5;
		}

		/**
		 * Distance from a world point to this solid's nearest surface, in world centimetres.
		 *
		 * Measured in the mesh's own frame and taken as a world distance, which is exact while the
		 * transform carries no scale. Every component HouseForge builds is placed at unit scale -
		 * geometry is generated at its real size rather than modelled small and scaled up, because a
		 * scaled mesh has no honest thickness - so this holds, and a caller that broke it would be
		 * breaking a great deal more than this figure.
		 */
		double DistanceToSurface(const FVector3d& World)
		{
			Build();

			const FVector3d Local = Surface->ToWorld.InverseTransformPosition(World);

			double DistanceSquared = 0.0;
			const int32 Triangle = Tree->FindNearestTriangle(Local, DistanceSquared);
			return Triangle >= 0 ? FMath::Sqrt(DistanceSquared) : 0.0;
		}
	};

	void BuildSolids(TArrayView<const FHFScanSurface> Surfaces, TArray<FSolid>& Out)
	{
		Out.Reset();
		Out.SetNum(Surfaces.Num());

		for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
		{
			FSolid& Solid = Out[Index];
			Solid.Surface = &Surfaces[Index];

			const FDynamicMesh3* Mesh = Surfaces[Index].Mesh;
			if (Mesh == nullptr || Mesh->TriangleCount() == 0)
			{
				continue;
			}

			for (const int32 Vertex : Mesh->VertexIndicesItr())
			{
				const FVector3d Local = Mesh->GetVertex(Vertex);
				Solid.LocalBounds.Contain(Local);
				Solid.WorldBounds.Contain(FVector3d(Surfaces[Index].ToWorld.TransformPosition(Local)));
			}
		}
	}

	/** True when two solids are worth comparing at all. */
	bool WorthComparing(const FSolid& A, const FSolid& B, double Tolerance)
	{
		if (A.Surface->Mesh == nullptr || B.Surface->Mesh == nullptr
			|| A.Surface->Mesh->TriangleCount() == 0 || B.Surface->Mesh->TriangleCount() == 0)
		{
			return false;
		}

		// Parts of one fixture are that fixture's own business. See the header.
		if (!A.Surface->Owner.IsNone() && A.Surface->Owner == B.Surface->Owner)
		{
			return false;
		}

		if (A.WorldBounds.IsEmpty() || B.WorldBounds.IsEmpty())
		{
			return false;
		}

		// Shrunk by the tolerance rather than grown: two solids that merely touch share a face, and
		// a face is not an interpenetration.
		FAxisAlignedBox3d Shared = A.WorldBounds;
		Shared.Intersect(B.WorldBounds);

		return Shared.Width() > Tolerance && Shared.Height() > Tolerance && Shared.Depth() > Tolerance;
	}

	/**
	 * The one pair comparison. Vertices first, then a grid.
	 *
	 * @return true when the pair interpenetrates past the tolerance.
	 */
	bool ComparePair(FSolid& A, FSolid& B, const FHFClashScanParams& Params, FHFClash& Out)
	{
		FAxisAlignedBox3d Shared = A.WorldBounds;
		Shared.Intersect(B.WorldBounds);

		// A hair of slack round the shared box, so a vertex sitting exactly on the far solid's face
		// is still offered to the winding test rather than culled by arithmetic.
		Shared.Expand(Params.DepthToleranceCm);

		double Deepest = 0.0;
		FVector3d DeepestAt = FVector3d::Zero();

		// ------------------------------------------------------------------------ the vertices
		//
		// WHERE TWO SOLIDS CROSS, THE DEEPEST INSIDE POINT IS A VERTEX OF ONE OF THEM. That is why
		// this is the measurement and the grid below is only a net: a grid fine enough to find the
		// deepest point of a 4 cm penetration would be finer than anything that could run over a
		// whole flat, and it would still be an approximation of a number available exactly.
		auto SweepVertices = [&Shared, &Deepest, &DeepestAt](FSolid& Probe, FSolid& Target)
		{
			const FDynamicMesh3& Mesh = *Probe.Surface->Mesh;
			const FTransform& ToWorld = Probe.Surface->ToWorld;

			for (const int32 Vertex : Mesh.VertexIndicesItr())
			{
				const FVector3d World(ToWorld.TransformPosition(Mesh.GetVertex(Vertex)));
				if (!Shared.Contains(World) || !Target.Contains(World))
				{
					continue;
				}

				const double Depth = Target.DistanceToSurface(World);
				if (Depth > Deepest)
				{
					Deepest = Depth;
					DeepestAt = World;
				}
			}
		};

		SweepVertices(A, B);
		SweepVertices(B, A);

		// ----------------------------------------------------------------------------- the grid
		//
		// Run TWICE, at two budgets, and the two runs are doing different jobs.
		//
		// The first is a net, over every pair in the flat whether or not there is anything in it, for
		// the crossing with no vertex of either solid inside the other: two thin plates passing
		// through each other, a rail through a post. It is deliberately coarse - see
		// FHFClashScanParams::MaxCrossingProbesPerPair, where the cost of getting this wrong is set
		// out. The second runs only where something has already been found, to put a volume on it.
		double CellVolume = 0.0;
		int64 Inside = 0;

		auto RunGrid = [&](int32 Budget)
		{
			const double Longest = FMath::Max3(Shared.Width(), Shared.Height(), Shared.Depth());
			double Pitch = FMath::Max(Params.SampleGridCm, KINDA_SMALL_NUMBER);

			auto CellsFor = [&Shared](double P)
			{
				return FMath::Max(1, FMath::CeilToInt(Shared.Width() / P))
					* static_cast<int64>(FMath::Max(1, FMath::CeilToInt(Shared.Height() / P)))
					* static_cast<int64>(FMath::Max(1, FMath::CeilToInt(Shared.Depth() / P)));
			};

			// Coarsened until it fits the budget, rather than the box clipped: a net that only looked
			// at part of a big overlap would miss whatever was in the rest of it.
			while (CellsFor(Pitch) > Budget && Pitch < Longest)
			{
				Pitch *= 1.5;
			}

			const int32 NX = FMath::Max(1, FMath::CeilToInt(Shared.Width() / Pitch));
			const int32 NY = FMath::Max(1, FMath::CeilToInt(Shared.Height() / Pitch));
			const int32 NZ = FMath::Max(1, FMath::CeilToInt(Shared.Depth() / Pitch));

			Inside = 0;
			CellVolume = (Shared.Width() / NX) * (Shared.Height() / NY) * (Shared.Depth() / NZ);

			for (int32 i = 0; i < NX; ++i)
			{
				for (int32 j = 0; j < NY; ++j)
				{
					for (int32 k = 0; k < NZ; ++k)
					{
						// Cell centres, so a sample never lands exactly on a shared face - where the
						// winding number is a coin toss and both answers are defensible.
						const FVector3d Point(
							Shared.Min.X + (i + 0.5) * Shared.Width() / NX,
							Shared.Min.Y + (j + 0.5) * Shared.Height() / NY,
							Shared.Min.Z + (k + 0.5) * Shared.Depth() / NZ);

						if (!A.Contains(Point) || !B.Contains(Point))
						{
							continue;
						}

						++Inside;

						const double Depth =
							FMath::Min(A.DistanceToSurface(Point), B.DistanceToSurface(Point));
						if (Depth > Deepest)
						{
							Deepest = Depth;
							DeepestAt = Point;
						}
					}
				}
			}
		};

		RunGrid(Params.MaxCrossingProbesPerPair);

		if (Deepest <= Params.DepthToleranceCm)
		{
			return false;
		}

		// Something is there. NOW it is worth spending samples on how much of it.
		if (Params.MaxSamplesPerPair > Params.MaxCrossingProbesPerPair)
		{
			RunGrid(Params.MaxSamplesPerPair);
		}

		Out.NameA = A.Surface->Name;
		Out.NameB = B.Surface->Name;
		Out.DepthCm = Deepest;
		Out.Sample = DeepestAt;
		Out.VolumeCm3 = static_cast<double>(Inside) * CellVolume;

		return true;
	}

	void SortDeepestFirst(TArray<FHFClash>& Clashes)
	{
		Clashes.Sort([](const FHFClash& L, const FHFClash& R) { return L.DepthCm > R.DepthCm; });
	}
}

TArray<FHFClash> FHFClashScan::Find(TArrayView<const FHFScanSurface> Surfaces,
	const FHFClashScanParams& Params)
{
	using namespace HouseForgeClash;

	TArray<FSolid> Solids;
	BuildSolids(Surfaces, Solids);

	TArray<FHFClash> Clashes;

	for (int32 i = 0; i < Solids.Num(); ++i)
	{
		for (int32 j = i + 1; j < Solids.Num(); ++j)
		{
			if (!WorthComparing(Solids[i], Solids[j], Params.DepthToleranceCm))
			{
				continue;
			}

			FHFClash Clash;
			if (ComparePair(Solids[i], Solids[j], Params, Clash))
			{
				Clashes.Add(MoveTemp(Clash));
			}
		}
	}

	SortDeepestFirst(Clashes);
	return Clashes;
}

TArray<FHFClash> FHFClashScan::FindBetween(TArrayView<const FHFScanSurface> Probes,
	TArrayView<const FHFScanSurface> Against, const FHFClashScanParams& Params)
{
	using namespace HouseForgeClash;

	TArray<FSolid> ProbeSolids;
	TArray<FSolid> AgainstSolids;
	BuildSolids(Probes, ProbeSolids);
	BuildSolids(Against, AgainstSolids);

	TArray<FHFClash> Clashes;

	for (FSolid& Probe : ProbeSolids)
	{
		for (FSolid& Target : AgainstSolids)
		{
			// The same surface can legitimately appear in both lists - a fixture's shell is what its
			// own parts are swept against on one call and a neighbour's parts on the next - so
			// identity is settled on the surface, not on the index.
			if (Probe.Surface == Target.Surface
				|| !WorthComparing(Probe, Target, Params.DepthToleranceCm))
			{
				continue;
			}

			FHFClash Clash;
			if (ComparePair(Probe, Target, Params, Clash))
			{
				Clashes.Add(MoveTemp(Clash));
			}
		}
	}

	SortDeepestFirst(Clashes);
	return Clashes;
}

double FHFClashScan::DeepestCm(TArrayView<const FHFClash> Clashes)
{
	double Deepest = 0.0;
	for (const FHFClash& Clash : Clashes)
	{
		Deepest = FMath::Max(Deepest, Clash.DepthCm);
	}
	return Deepest;
}

TArray<FString> FHFClashScan::Describe(TArrayView<const FHFClash> Clashes, int32 MaxLines)
{
	TArray<FHFClash> Sorted(Clashes);
	HouseForgeClash::SortDeepestFirst(Sorted);

	TArray<FString> Lines;
	const int32 Count = FMath::Min(Sorted.Num(), FMath::Max(MaxLines, 0));

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FHFClash& Clash = Sorted[Index];
		Lines.Add(FString::Printf(
			TEXT("'%s' stands %.2f cm inside '%s' (about %.0f cm3), at (%.1f, %.1f, %.1f)."),
			*Clash.NameA, Clash.DepthCm, *Clash.NameB, Clash.VolumeCm3,
			Clash.Sample.X, Clash.Sample.Y, Clash.Sample.Z));
	}

	if (Sorted.Num() > Count)
	{
		Lines.Add(FString::Printf(TEXT("... and %d more."), Sorted.Num() - Count));
	}

	return Lines;
}
