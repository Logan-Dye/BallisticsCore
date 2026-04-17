// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BallisticsCoreSettings.generated.h"

/**
 * Authority model for projectile firing.
 * Set globally in DefaultGame.ini, individual launchers can override.
 */
UENUM(BlueprintType)
enum class EFireAuthorityMode : uint8
{
	/** Server spawns and simulates. Clients see replicated results. Simpler, cheat-proof, but adds visible latency. */
	ServerAuthoritative,

	/** Client spawns locally for instant feedback. Server validates. Better feel, needs lag compensation. */
	ClientPredicted
};

/**
 * Global configuration for the BallisticsCore plugin.
 * Edit in Project Settings → Game → Ballistics Core, or directly in DefaultGame.ini:
 *
 *   [/Script/BallisticsCoreRuntime.UBallisticsCoreSettings]
 *   FireAuthorityMode=ClientPredicted
 *   MaxLagCompensationWindow=0.25
 *   DefaultProjectilePoolSize=64
 *   bEnableTickBatching=true
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Ballistics Core"))
class BALLISTICSCORERUNTIME_API UBallisticsCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBallisticsCoreSettings();

	/** Get the singleton settings instance */
	static const UBallisticsCoreSettings* Get();

	// ─── Authority ───────────────────────────────

	/** Global fire authority mode. Individual launchers can override this. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority")
	EFireAuthorityMode FireAuthorityMode;

	/** Max time window (seconds) for lag compensation in ClientPredicted mode. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority", meta = (ClampMin = "0", ClampMax = "1.0", EditCondition = "FireAuthorityMode == EFireAuthorityMode::ClientPredicted"))
	float MaxLagCompensationWindow;

	/** Max distance tolerance (cm) when reconciling client prediction with server state. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority", meta = (ClampMin = "0"))
	float PredictionReconciliationTolerance;

	// ─── Pooling ─────────────────────────────────

	/** Enable projectile object pooling. Reduces spawn/destroy overhead for high fire-rate weapons. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bEnableProjectilePooling;

	/** Number of projectile actors to pre-spawn per pool. Grows dynamically if exceeded. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "4", ClampMax = "512", EditCondition = "bEnableProjectilePooling"))
	int32 DefaultProjectilePoolSize;

	// ─── Tick Batching ───────────────────────────

	/** Batch-tick all active projectiles through the subsystem instead of individual component ticks. Better cache coherency for high projectile counts. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bEnableTickBatching;

	// ─── Physics ─────────────────────────────────

	/** Global gravity override (cm/s^2). Negative = downward. Set to 0 for zero-G environments. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Physics")
	float GravityZ;

	/** Global air density (g/cm^3). 0.001225 = sea level Earth. Reduce for high altitude or thin atmospheres. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Physics", meta = (ClampMin = "0"))
	float AirDensity;

	// ─── Category path for Project Settings ──────

	virtual FName GetCategoryName() const override { return FName("Game"); }
};
