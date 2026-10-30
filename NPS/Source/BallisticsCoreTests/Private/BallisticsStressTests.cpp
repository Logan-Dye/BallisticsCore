// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "BallisticsTrajectoryLibrary.h"
#include "BallisticsTypes.h"
#include "HAL/PlatformTime.h"

static FProjectileInitialConditions MakeStressTestConditions(int32 Seed = 42)
{
	FProjectileInitialConditions C;
	C.Origin = FVector::ZeroVector;
	C.Direction = FVector::ForwardVector;
	C.InitialSpeed = 90000.f;
	C.Mass = 9.5f;
	C.Diameter = 0.9f;
	C.DragCoefficient = 0.4f;
	C.RandomSeed = Seed;
	C.WindAcceleration = FVector(0.f, 100.f, 0.f);
	return C;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStressTest1K,
	"NPS.Stress.1K_TrajectoryEvaluations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FStressTest1K::RunTest(const FString& Parameters)
{
	const int32 Count = 1000;
	const float SimDuration = 2.0f;

	const double StartTime = FPlatformTime::Seconds();

	for (int32 i = 0; i < Count; ++i)
	{
		FProjectileInitialConditions Conditions = MakeStressTestConditions(i);
		Conditions.Direction = FVector(1.f, FMath::Sin(static_cast<float>(i) * 0.1f), FMath::Cos(static_cast<float>(i) * 0.1f)).GetSafeNormal();

		UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Conditions, SimDuration);
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	const double PerEvalUs = (ElapsedMs * 1000.0) / Count;

	AddInfo(FString::Printf(TEXT("1K trajectory evals: %.2f ms total, %.2f us/eval"), ElapsedMs, PerEvalUs));

	TestTrue(
		FString::Printf(TEXT("1K evals completed in %.2f ms (budget: 100 ms)"), ElapsedMs),
		ElapsedMs < 100.0
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStressTest10K,
	"NPS.Stress.10K_TrajectoryEvaluations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FStressTest10K::RunTest(const FString& Parameters)
{
	const int32 Count = 10000;
	const float SimDuration = 2.0f;

	const double StartTime = FPlatformTime::Seconds();

	for (int32 i = 0; i < Count; ++i)
	{
		FProjectileInitialConditions Conditions = MakeStressTestConditions(i);
		Conditions.Direction = FVector(1.f, FMath::Sin(static_cast<float>(i) * 0.01f), FMath::Cos(static_cast<float>(i) * 0.01f)).GetSafeNormal();

		UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Conditions, SimDuration);
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	const double PerEvalUs = (ElapsedMs * 1000.0) / Count;

	AddInfo(FString::Printf(TEXT("10K trajectory evals: %.2f ms total, %.2f us/eval"), ElapsedMs, PerEvalUs));

	TestTrue(
		FString::Printf(TEXT("10K evals completed in %.2f ms (budget: 500 ms)"), ElapsedMs),
		ElapsedMs < 500.0
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStressTest100K,
	"NPS.Stress.100K_TrajectoryEvaluations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FStressTest100K::RunTest(const FString& Parameters)
{
	const int32 Count = 100000;
	const float SimDuration = 2.0f;

	const double StartTime = FPlatformTime::Seconds();

	for (int32 i = 0; i < Count; ++i)
	{
		FProjectileInitialConditions Conditions = MakeStressTestConditions(i % 256);
		Conditions.Direction = FVector(1.f, FMath::Sin(static_cast<float>(i) * 0.001f), FMath::Cos(static_cast<float>(i) * 0.001f)).GetSafeNormal();

		UBallisticsTrajectoryLibrary::EvaluateSnapshotAtTime(Conditions, SimDuration);
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	const double PerEvalUs = (ElapsedMs * 1000.0) / Count;

	AddInfo(FString::Printf(TEXT("100K trajectory evals: %.2f ms total, %.2f us/eval"), ElapsedMs, PerEvalUs));

	TestTrue(
		FString::Printf(TEXT("100K evals completed in %.2f ms (budget: 5000 ms)"), ElapsedMs),
		ElapsedMs < 5000.0
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStressTest1M,
	"NPS.Stress.1M_TrajectoryEvaluations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FStressTest1M::RunTest(const FString& Parameters)
{
	const int32 Count = 1000000;
	const float SimDuration = 1.0f;

	const double StartTime = FPlatformTime::Seconds();

	for (int32 i = 0; i < Count; ++i)
	{
		FProjectileInitialConditions Conditions = MakeStressTestConditions(i % 256);
		Conditions.Direction = FVector(1.f, FMath::Sin(static_cast<float>(i) * 0.0001f), FMath::Cos(static_cast<float>(i) * 0.0001f)).GetSafeNormal();

		UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(Conditions, SimDuration);
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	const double PerEvalUs = (ElapsedMs * 1000.0) / Count;
	const double EvalsPerSecond = Count / (ElapsedMs / 1000.0);

	AddInfo(FString::Printf(TEXT("1M trajectory evals: %.2f ms total, %.2f us/eval, %.0f evals/sec"), ElapsedMs, PerEvalUs, EvalsPerSecond));

	TestTrue(
		FString::Printf(TEXT("1M evals completed in %.2f ms (budget: 30000 ms)"), ElapsedMs),
		ElapsedMs < 30000.0
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStressTestBatchTick,
	"NPS.Stress.BatchTick_1000Projectiles_60FPS",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FStressTestBatchTick::RunTest(const FString& Parameters)
{
	const int32 ProjectileCount = 1000;
	const float DeltaTime = 1.f / 60.f; // 16.67ms
	const float TotalDuration = 2.0f;
	const int32 FrameCount = FMath::CeilToInt(TotalDuration / DeltaTime);

	struct FSimProjectile
	{
		FProjectileInitialConditions Conditions;
		FVector Position;
		FVector Velocity;
	};

	TArray<FSimProjectile> Projectiles;
	Projectiles.SetNum(ProjectileCount);

	for (int32 i = 0; i < ProjectileCount; ++i)
	{
		Projectiles[i].Conditions = MakeStressTestConditions(i);
		Projectiles[i].Conditions.Direction = FVector(1.f, FMath::Sin(static_cast<float>(i) * 0.05f), 0.f).GetSafeNormal();
		Projectiles[i].Position = Projectiles[i].Conditions.Origin;
		Projectiles[i].Velocity = Projectiles[i].Conditions.Direction * Projectiles[i].Conditions.InitialSpeed;
	}

	const double StartTime = FPlatformTime::Seconds();

	for (int32 Frame = 0; Frame < FrameCount; ++Frame)
	{
		for (int32 i = 0; i < ProjectileCount; ++i)
		{
			FSimProjectile& Proj = Projectiles[i];
			const FVector Accel = UBallisticsTrajectoryLibrary::ComputeAcceleration(Proj.Conditions, Proj.Velocity);
			Proj.Position += Proj.Velocity * DeltaTime + 0.5f * Accel * DeltaTime * DeltaTime;
			Proj.Velocity += Accel * DeltaTime;
		}
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	const double MsPerFrame = ElapsedMs / FrameCount;
	const int32 TotalTicks = ProjectileCount * FrameCount;

	AddInfo(FString::Printf(TEXT("Batch tick: %d projectiles x %d frames = %d ticks"), ProjectileCount, FrameCount, TotalTicks));
	AddInfo(FString::Printf(TEXT("Total: %.2f ms, Per-frame: %.3f ms (budget: 16.67 ms)"), ElapsedMs, MsPerFrame));

	TestTrue(
		FString::Printf(TEXT("Per-frame batch tick cost (%.3f ms) is under 2ms"), MsPerFrame),
		MsPerFrame < 2.0
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStressTestFlightLogCreation,
	"NPS.Stress.FlightLogCreation_10K",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FStressTestFlightLogCreation::RunTest(const FString& Parameters)
{
	const int32 Count = 10000;

	const double StartTime = FPlatformTime::Seconds();

	for (int32 i = 0; i < Count; ++i)
	{
		FProjectileFlightLog FlightLog;
		FlightLog.LaunchID = i;
		FlightLog.ClientTimestamp = 100.f + static_cast<float>(i) * 0.016f;
		FlightLog.InitialConditions = MakeStressTestConditions(i % 256);

		FProjectileFlightSegment Seg1;
		Seg1.StartLocation = FVector::ZeroVector;
		Seg1.EndLocation = FVector(3000.f, 0.f, 0.f);
		Seg1.ImpactResponse = EImpactResponse::Ricochet;
		Seg1.ImpactNormal = FVector(0.f, 1.f, 0.f);
		Seg1.SpeedAtStart = 90000.f;
		FlightLog.Segments.Add(Seg1);

		FProjectileFlightSegment Seg2;
		Seg2.StartLocation = Seg1.EndLocation;
		Seg2.EndLocation = FVector(3000.f, 2000.f, 0.f);
		Seg2.ImpactResponse = EImpactResponse::Penetrate;
		Seg2.ImpactNormal = FVector(-1.f, 0.f, 0.f);
		Seg2.SpeedAtStart = 60000.f;
		FlightLog.Segments.Add(Seg2);

		FProjectileFlightSegment Seg3;
		Seg3.StartLocation = Seg2.EndLocation;
		Seg3.EndLocation = FVector(3000.f, 2000.f, 500.f);
		Seg3.ImpactResponse = EImpactResponse::Embed;
		Seg3.SpeedAtStart = 30000.f;
		FlightLog.Segments.Add(Seg3);

		check(FlightLog.Segments.Num() == 3);
		check(FlightLog.LaunchID == i);
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	const double PerLogUs = (ElapsedMs * 1000.0) / Count;

	AddInfo(FString::Printf(TEXT("10K flight log creation: %.2f ms total, %.2f us/log"), ElapsedMs, PerLogUs));

	TestTrue(
		FString::Printf(TEXT("10K flight log creation completed in %.2f ms (budget: 100 ms)"), ElapsedMs),
		ElapsedMs < 100.0
	);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FStressTestDeterminism,
	"NPS.Stress.DeterminismAtVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter
)

bool FStressTestDeterminism::RunTest(const FString& Parameters)
{
	// Run 10K evaluations, then run the same 10K again and check bit-identical
	const int32 Count = 10000;
	const float SimDuration = 1.5f;

	TArray<FVector> Results1;
	TArray<FVector> Results2;
	Results1.SetNum(Count);
	Results2.SetNum(Count);

	for (int32 i = 0; i < Count; ++i)
	{
		FProjectileInitialConditions Conditions = MakeStressTestConditions(i % 256);
		Conditions.Direction = FVector(1.f, FMath::Sin(static_cast<float>(i) * 0.01f), 0.f).GetSafeNormal();
		Results1[i] = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(Conditions, SimDuration);
	}

	for (int32 i = 0; i < Count; ++i)
	{
		FProjectileInitialConditions Conditions = MakeStressTestConditions(i % 256);
		Conditions.Direction = FVector(1.f, FMath::Sin(static_cast<float>(i) * 0.01f), 0.f).GetSafeNormal();
		Results2[i] = UBallisticsTrajectoryLibrary::EvaluatePositionAtTime(Conditions, SimDuration);
	}

	// Compare - must be bit-identical
	int32 Mismatches = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		if (Results1[i] != Results2[i])
		{
			Mismatches++;
			if (Mismatches <= 5)
			{
				AddError(FString::Printf(TEXT("Mismatch at index %d: (%.4f, %.4f, %.4f) vs (%.4f, %.4f, %.4f)"),
					i, Results1[i].X, Results1[i].Y, Results1[i].Z,
					Results2[i].X, Results2[i].Y, Results2[i].Z));
			}
		}
	}

	TestEqual("Zero mismatches across 10K deterministic evaluations", Mismatches, 0);

	return true;
}
