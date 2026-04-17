// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "BallisticsTrajectoryLibrary.h"
#include "BallisticsTypes.h"

// Helper: build standard test conditions (rifle round)
static FProjectileInitialConditions MakeTestConditions()
{
	FProjectileInitialConditions C;
	C.Origin = FVector::ZeroVector;
	C.Direction = FVector::ForwardVector;
	C.MuzzleVelocity = 90000.f;  // 900 m/s rifle round
	C.Mass = 9.5f;               // grams
	C.Diameter = 0.9f;           // cm (9mm)
	C.DragCoefficient = 0.4f;
	C.RandomSeed = 42;
	C.WindAcceleration = FVector::ZeroVector;
	return C;
}

// ─────────────────────────────────────────────
//  Determinism: same inputs → identical outputs
// ─────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryDeterminismTest,
	"BallisticsCore.Trajectory.Determinism.SameInputsSameOutput",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FTrajectoryDeterminismTest::RunTest(const FString& Parameters)
{
	const FProjectileInitialConditions Conditions = MakeTestConditions();

	// Evaluate the same trajectory twice at multiple time points
	const TArray<float> TimePoints = { 0.1f, 0.5f, 1.0f, 2.0f, 5.0f };

	for (const float Time : TimePoints)
	{
		const FVector Pos1 = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(Conditions, Time);
		const FVector Pos2 = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(Conditions, Time);

		// Must be bit-identical, not approximately equal
		TestEqual(FString::Printf(TEXT("X at t=%.1f"), Time), Pos1.X, Pos2.X);
		TestEqual(FString::Printf(TEXT("Y at t=%.1f"), Time), Pos1.Y, Pos2.Y);
		TestEqual(FString::Printf(TEXT("Z at t=%.1f"), Time), Pos1.Z, Pos2.Z);
	}

	return true;
}

// ─────────────────────────────────────────────
//  Different seeds produce different trajectories
//  (when randomness is involved — for now just
//   verifying the seed is stored correctly)
// ─────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectorySeedTest,
	"BallisticsCore.Trajectory.Determinism.DifferentSeedsDifferentStreams",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FTrajectorySeedTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream1(42);
	FRandomStream Stream2(99);

	// Different seeds should produce different random values
	const float Val1 = Stream1.FRandRange(-5.f, 5.f);
	const float Val2 = Stream2.FRandRange(-5.f, 5.f);

	TestNotEqual("Different seeds produce different random values", Val1, Val2);

	// Same seed should reproduce the same value
	FRandomStream Stream1Again(42);
	const float Val1Again = Stream1Again.FRandRange(-5.f, 5.f);
	TestEqual("Same seed reproduces same value", Val1, Val1Again);

	return true;
}

// ─────────────────────────────────────────────
//  Zero drag matches analytic parabola
// ─────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryNoDragParabolaTest,
	"BallisticsCore.Trajectory.Physics.NoDragMatchesParabola",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FTrajectoryNoDragParabolaTest::RunTest(const FString& Parameters)
{
	FProjectileInitialConditions Conditions = MakeTestConditions();
	Conditions.DragCoefficient = 0.f; // No drag

	const float Time = 1.0f;
	const FVector Result = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(Conditions, Time);

	// Analytic parabola: Pos = Origin + V*t + 0.5*g*t^2
	const FVector Expected = Conditions.Origin
		+ (Conditions.Direction * Conditions.MuzzleVelocity * Time)
		+ FVector(0.f, 0.f, 0.5f * -980.f * Time * Time);

	// With no drag, the piecewise solver should match the analytic solution closely
	// Allow small tolerance from floating-point accumulation over many sub-steps
	const float Tolerance = 1.0f; // 1 cm tolerance over 1 second

	TestTrue(
		FString::Printf(TEXT("Position within tolerance (error: %.4f cm)"), FVector::Dist(Result, Expected)),
		FVector::Dist(Result, Expected) < Tolerance
	);

	return true;
}

// ─────────────────────────────────────────────
//  Drag reduces speed over time
// ─────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryDragReducesSpeedTest,
	"BallisticsCore.Trajectory.Physics.DragReducesSpeed",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FTrajectoryDragReducesSpeedTest::RunTest(const FString& Parameters)
{
	const FProjectileInitialConditions Conditions = MakeTestConditions();

	const FProjectileSnapshot Early = UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Conditions, 0.1f);
	const FProjectileSnapshot Later = UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Conditions, 2.0f);

	// Speed should decrease over time due to drag
	TestTrue(
		FString::Printf(TEXT("Speed at 0.1s (%.1f) > Speed at 2.0s (%.1f)"), Early.Speed, Later.Speed),
		Early.Speed > Later.Speed
	);

	return true;
}

// ─────────────────────────────────────────────
//  Wind deflects the trajectory
// ─────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryWindDeflectionTest,
	"BallisticsCore.Trajectory.Physics.WindDeflectsTrajectory",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FTrajectoryWindDeflectionTest::RunTest(const FString& Parameters)
{
	FProjectileInitialConditions NoWind = MakeTestConditions();
	FProjectileInitialConditions WithWind = MakeTestConditions();
	WithWind.WindAcceleration = FVector(0.f, 500.f, 0.f); // Strong crosswind

	const float Time = 2.0f;
	const FVector PosNoWind = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(NoWind, Time);
	const FVector PosWithWind = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(WithWind, Time);

	// The wind version should be deflected in the Y axis
	const float YDeflection = FMath::Abs(PosWithWind.Y - PosNoWind.Y);

	TestTrue(
		FString::Printf(TEXT("Y deflection from wind: %.1f cm (should be > 100)"), YDeflection),
		YDeflection > 100.f
	);

	return true;
}

// ─────────────────────────────────────────────
//  SolveLaunchDirection finds a solution for
//  a straight-ahead target
// ─────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectorySolveLaunchTest,
	"BallisticsCore.Trajectory.Solver.FindsSolutionForNearTarget",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter
)

bool FTrajectorySolveLaunchTest::RunTest(const FString& Parameters)
{
	const FProjectileInitialConditions Conditions = MakeTestConditions();
	const FVector Origin = FVector::ZeroVector;
	const FVector Target = FVector(5000.f, 0.f, 0.f); // 50m ahead

	FVector Direction;
	float TimeOfFlight;
	const bool bFound = UBallisticsTrajectoryLibrary::SolveLaunchDirection(
		Origin, Target, Conditions, Direction, TimeOfFlight
	);

	TestTrue("Solver found a solution", bFound);

	if (bFound)
	{
		// Verify the solved direction actually gets close to the target
		FProjectileInitialConditions SolvedConditions = Conditions;
		SolvedConditions.Origin = Origin;
		SolvedConditions.Direction = Direction;

		const FVector LandingPos = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(SolvedConditions, TimeOfFlight);
		const float Error = FVector::Dist(LandingPos, Target);

		TestTrue(
			FString::Printf(TEXT("Landing error: %.1f cm (should be < 50)"), Error),
			Error < 50.f
		);
	}

	return true;
}
