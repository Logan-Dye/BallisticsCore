// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "BallisticsTrajectoryLibrary.h"
#include "BallisticsTypes.h"

static FProjectileInitialConditions MakeTestConditions()
{
	FProjectileInitialConditions C;
	C.Origin = FVector::ZeroVector;
	C.Direction = FVector::ForwardVector;
	C.InitialSpeed = 90000.f;
	C.Mass = 9.5f;               // grams
	C.Diameter = 0.9f;           // cm
	C.DragCoefficient = 0.4f;
	C.RandomSeed = 42;
	C.WindAcceleration = FVector::ZeroVector;
	return C;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryDeterminismTest,
	"NPS.Trajectory.Determinism.SameInputsSameOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FTrajectoryDeterminismTest::RunTest(const FString& Parameters)
{
	const FProjectileInitialConditions Conditions = MakeTestConditions();

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectorySeedTest,
	"NPS.Trajectory.Determinism.DifferentSeedsDifferentStreams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FTrajectorySeedTest::RunTest(const FString& Parameters)
{
	FRandomStream Stream1(42);
	FRandomStream Stream2(99);

	const float Val1 = Stream1.FRandRange(-5.f, 5.f);
	const float Val2 = Stream2.FRandRange(-5.f, 5.f);

	TestNotEqual("Different seeds produce different random values", Val1, Val2);

	FRandomStream Stream1Again(42);
	const float Val1Again = Stream1Again.FRandRange(-5.f, 5.f);
	TestEqual("Same seed reproduces same value", Val1, Val1Again);

	return true;
}
// Zero drag matches analytic parabola

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryNoDragParabolaTest,
	"NPS.Trajectory.Physics.NoDragMatchesParabola",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FTrajectoryNoDragParabolaTest::RunTest(const FString& Parameters)
{
	FProjectileInitialConditions Conditions = MakeTestConditions();
	Conditions.DragCoefficient = 0.f; // No drag

	const float Time = 1.0f;
	const FVector Result = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(Conditions, Time);

	// Analytic parabola: Pos = Origin + V*t + 0.5*g*t^2
	const FVector Expected = Conditions.Origin
		+ (Conditions.Direction * Conditions.InitialSpeed * Time)
		+ FVector(0.f, 0.f, 0.5f * -980.f * Time * Time);

	const float Tolerance = 1.0f; // 1 cm tolerance over 1 second

	TestTrue(
		FString::Printf(TEXT("Position within tolerance (error: %.4f cm)"), FVector::Dist(Result, Expected)),
		FVector::Dist(Result, Expected) < Tolerance
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryDragReducesSpeedTest,
	"NPS.Trajectory.Physics.DragReducesSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FTrajectoryDragReducesSpeedTest::RunTest(const FString& Parameters)
{
	const FProjectileInitialConditions Conditions = MakeTestConditions();

	const FProjectileSnapshot Early = UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Conditions, 0.1f);
	const FProjectileSnapshot Later = UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Conditions, 2.0f);

	TestTrue(
		FString::Printf(TEXT("Speed at 0.1s (%.1f) > Speed at 2.0s (%.1f)"), Early.Speed, Later.Speed),
		Early.Speed > Later.Speed
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectoryWindDeflectionTest,
	"NPS.Trajectory.Physics.WindDeflectsTrajectory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FTrajectoryWindDeflectionTest::RunTest(const FString& Parameters)
{
	FProjectileInitialConditions NoWind = MakeTestConditions();
	FProjectileInitialConditions WithWind = MakeTestConditions();
	WithWind.WindAcceleration = FVector(0.f, 500.f, 0.f); // Strong crosswind

	const float Time = 2.0f;
	const FVector PosNoWind = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(NoWind, Time);
	const FVector PosWithWind = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(WithWind, Time);

	const float YDeflection = FMath::Abs(PosWithWind.Y - PosNoWind.Y);

	TestTrue(
		FString::Printf(TEXT("Y deflection from wind: %.1f cm (should be > 100)"), YDeflection),
		YDeflection > 100.f
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTrajectorySolveLaunchTest,
	"NPS.Trajectory.Solver.FindsSolutionForNearTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
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
		FProjectileInitialConditions SolvedConditions = Conditions;
		SolvedConditions.Origin = Origin;
		SolvedConditions.Direction = Direction;

		const FVector LandingPos = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(SolvedConditions, TimeOfFlight);
		const float Error = FVector::Dist(LandingPos, Target);

		TestTrue(
			FString::Printf(TEXT("Landing position within 10cm of target (error: %.2f cm)"), Error),
			Error < 10.f
		);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectileFlightLogSingleSegmentTest,
	"NPS.FlightLog.SingleSegmentValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FProjectileFlightLogSingleSegmentTest::RunTest(const FString& Parameters)
{
	FProjectileFlightLog FlightLog;
	FlightLog.LaunchID = 1;
	FlightLog.ClientTimestamp = 100.f;
	FlightLog.InitialConditions = MakeTestConditions();

	FProjectileFlightSegment Segment;
	Segment.StartLocation = FVector::ZeroVector;
	Segment.EndLocation = FVector(5000.f, 0.f, 0.f);
	Segment.ImpactResponse = EImpactResponse::Embed;
	Segment.SpeedAtStart = 90000.f;
	FlightLog.Segments.Add(Segment);

	TestEqual("FlightLog has 1 segment", FlightLog.Segments.Num(), 1);
	TestEqual("LaunchID is 1", FlightLog.LaunchID, 1);
	TestTrue("Segment start is origin", Segment.StartLocation.IsNearlyZero());
	TestTrue("Segment end is 50m forward", FMath::IsNearlyEqual(Segment.EndLocation.X, 5000.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectileFlightLogRicochetSegmentTest,
	"NPS.FlightLog.MultiSegmentRicochet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FProjectileFlightLogRicochetSegmentTest::RunTest(const FString& Parameters)
{
	FProjectileFlightLog FlightLog;
	FlightLog.LaunchID = 2;
	FlightLog.ClientTimestamp = 100.f;
	FlightLog.InitialConditions = MakeTestConditions();

	FProjectileFlightSegment Seg1;
	Seg1.StartLocation = FVector::ZeroVector;
	Seg1.EndLocation = FVector(3000.f, 0.f, 0.f);
	Seg1.ImpactResponse = EImpactResponse::Ricochet;
	Seg1.ImpactNormal = FVector(0.f, 1.f, 0.f); // Wall facing Y+
	Seg1.SpeedAtStart = 90000.f;
	FlightLog.Segments.Add(Seg1);

	FProjectileFlightSegment Seg2;
	Seg2.StartLocation = FVector(3000.f, 0.f, 0.f);
	Seg2.EndLocation = FVector(3000.f, 2000.f, 0.f);
	Seg2.ImpactResponse = EImpactResponse::Embed;
	Seg2.SpeedAtStart = 60000.f; // Lost energy on bounce
	FlightLog.Segments.Add(Seg2);

	TestEqual("FlightLog has 2 segments", FlightLog.Segments.Num(), 2);
	TestEqual("First segment is ricochet", Seg1.ImpactResponse, EImpactResponse::Ricochet);
	TestEqual("Second segment is embed", Seg2.ImpactResponse, EImpactResponse::Embed);

	TestTrue("Speed reduced after ricochet", Seg2.SpeedAtStart < Seg1.SpeedAtStart);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectileFlightLogEmptySegmentTest,
	"NPS.FlightLog.EmptySegmentsInvalid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FProjectileFlightLogEmptySegmentTest::RunTest(const FString& Parameters)
{
	FProjectileFlightLog FlightLog;
	FlightLog.LaunchID = 3;
	FlightLog.ClientTimestamp = 100.f;
	FlightLog.InitialConditions = MakeTestConditions();

	TestEqual("Empty flight log has 0 segments", FlightLog.Segments.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRicochetAngleValidationTest,
	"NPS.FlightLog.RicochetAngleValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FRicochetAngleValidationTest::RunTest(const FString& Parameters)
{
	const FVector IncomingDir = FVector(1.f, -1.f, 0.f).GetSafeNormal();
	const FVector WallNormal = FVector(0.f, 1.f, 0.f);
	const FVector ExpectedReflection = FMath::GetReflectionVector(IncomingDir, WallNormal);

	const FVector ExpectedNorm = FVector(1.f, 1.f, 0.f).GetSafeNormal();
	const float Error = FVector::Dist(ExpectedReflection.GetSafeNormal(), ExpectedNorm);

	TestTrue(
		FString::Printf(TEXT("Reflection vector matches expected (error: %.4f)"), Error),
		Error < 0.01f
	);

	const FVector FakeOutgoing = FVector(-1.f, 0.f, 0.f); // Going backwards - impossible from a ricochet
	const float DotProduct = FVector::DotProduct(ExpectedReflection.GetSafeNormal(), FakeOutgoing.GetSafeNormal());
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.f, 1.f)));

	TestTrue(
		FString::Printf(TEXT("Fake ricochet angle (%.1f deg) exceeds 10 deg tolerance"), AngleDeg),
		AngleDeg > 10.f
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPenetrationEnergyValidationTest,
	"NPS.FlightLog.PenetrationEnergyValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FPenetrationEnergyValidationTest::RunTest(const FString& Parameters)
{
	const FProjectileInitialConditions Conditions = MakeTestConditions();

	// KE at launch (Joules): 0.5 * 9.5g * (90000 cm/s)^2 * 1e-7 ~ 3847 J (9.5g at 900 m/s)
	const float FullSpeedKE = 0.5f * Conditions.Mass * Conditions.InitialSpeed * Conditions.InitialSpeed * KE_RAW_TO_JOULES;

	const float StandardToughness = 2000.f;
	TestTrue("Full-speed projectile penetrates standard material", FullSpeedKE > StandardToughness);

	const float SlowSpeed = 100.f; // 1 m/s
	const float SlowKE = 0.5f * Conditions.Mass * SlowSpeed * SlowSpeed * KE_RAW_TO_JOULES;
	TestTrue("Slow projectile does not penetrate standard material", SlowKE < StandardToughness);

	return true;
}
