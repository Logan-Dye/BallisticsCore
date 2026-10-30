// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Check each PIE world so an Iris validation run cannot silently exercise legacy replication.
inline bool ValidateNPSNetworkBackend(FAutomationTestBase& Test, const UWorld* World)
{
	const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
	if (!Driver)
	{
		Test.AddError(TEXT("NPS backend validation requires an active net driver."));
		return false;
	}

	bool bUsingIris = false;
#if UE_WITH_IRIS
	bUsingIris = Driver->IsUsingIrisReplication();
	if (bUsingIris && !Driver->GetReplicationSystem())
	{
		Test.AddError(TEXT("NPS Iris net driver has no replication system."));
		return false;
	}
#endif
	Test.AddInfo(FString::Printf(TEXT("NPS backend: %s; world=%s"),
		bUsingIris ? TEXT("Iris") : TEXT("Legacy"), *World->GetName()));

	int32 ExpectedIris = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NPSExpectedIris="), ExpectedIris))
	{
		if ((ExpectedIris != 0 && ExpectedIris != 1) || bUsingIris != (ExpectedIris == 1))
		{
			Test.AddError(FString::Printf(
				TEXT("NPSExpectedIris=%d does not match the active backend (Iris=%d)."),
				ExpectedIris, bUsingIris ? 1 : 0));
			return false;
		}
	}
	return true;
}
