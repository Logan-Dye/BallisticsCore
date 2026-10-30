// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Engine/DeveloperSettings.h"
#include "BallisticsAggregateTick.h"
#include "BallisticsCosmeticTypes.h"
#include "BallisticsCoreSettings.generated.h"

/**
 * Authority model for projectile launching.
 * Set globally in Project Settings -> Game -> Networked Projectile System.
 */
UENUM(BlueprintType)
enum class ELaunchAuthorityMode : uint8
{
	/** Server spawns and simulates. Clients see replicated results. Strongest server control, with added visible latency. */
	ServerAuthoritative,

	/** Client spawns locally for instant feedback. Server validates via resimulation. Better feel, needs lag compensation. */
	ClientPredicted,

	/** Client has full launch authority. Server validates via flight log (visibility, angle, material checks). Most responsive, least cheat-resistant. */
	ClientAuthoritative
};

/**
 * How the launching client sees their own projectile in Server Authoritative mode.
 * Only relevant when LaunchAuthorityMode == ServerAuthoritative.
 */
UENUM(BlueprintType)
enum class EServerAuthCosmeticMode : uint8
{
	/** No local cosmetic. Launching client sees their projectile only after the server multicast arrives (full RTT delay). */
	NoLocalCosmetic,

	/** Spawn an immediate local cosmetic projectile on input. Hides latency but cosmetic may diverge from server. */
	ImmediateLocalCosmetic,

	/** Spawn a cosmetic that fast-forwards by half-RTT to approximate where the server projectile is. */
	FastForwardedCosmetic
};

/**
 * Global configuration for the Networked Projectile System (NPS) plugin.
 * Edit in Project Settings -> Game -> Networked Projectile System, or directly in DefaultGame.ini:
 *
 *   [/Script/BallisticsCoreRuntime.BallisticsCoreSettings]
 *   LaunchAuthorityMode=ClientPredicted
 *   MaxLagCompensationWindow=0.25
 *   DefaultProjectilePoolSize=64
 *   bEnableTickBatching=true
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Networked Projectile System"))
class BALLISTICSCORERUNTIME_API UBallisticsCoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBallisticsCoreSettings();

	/** Get the singleton settings instance */
	static const UBallisticsCoreSettings* Get();

	// Authority

	/** Global launch authority mode. Determines how projectile spawning/validation works across the network. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority",
		meta = (ToolTip = "ServerAuthoritative: safest, highest latency. ClientPredicted: balanced. ClientAuthoritative: lowest latency, most trust in client."))
	ELaunchAuthorityMode LaunchAuthorityMode;

	// Server Authoritative Settings

	/** How the launching client sees their own projectile in Server Authoritative mode. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Server Authoritative",
		meta = (EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ServerAuthoritative"))
	EServerAuthCosmeticMode ServerAuthCosmeticMode;

	// Client Predicted Settings

	/** Max time window (seconds) for lag compensation in ClientPredicted mode. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Predicted",
		meta = (ClampMin = "0", ClampMax = "1.0",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientPredicted"))
	float MaxLagCompensationWindow;

	/** Max distance tolerance (cm) when reconciling client prediction with server state. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Predicted",
		meta = (ClampMin = "0",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientPredicted"))
	float PredictionReconciliationTolerance;

	// Client Authoritative Settings

	/** Max number of flight logs the server stores per player. Oldest is dropped when full (ring buffer). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Authoritative",
		meta = (ClampMin = "8", ClampMax = "256",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientAuthoritative"))
	int32 MaxSavedFlightLogs;

	/** Max age (seconds) of a flight log before the server rejects it as expired. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Authoritative",
		meta = (ClampMin = "0.5", ClampMax = "10.0",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientAuthoritative"))
	float MaxFlightLogAge;

	/** Tolerance (degrees) for ricochet angle validation. How far off the client's ricochet angle can be from the expected angle before the server rejects it. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Authoritative",
		meta = (ClampMin = "0", ClampMax = "45",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientAuthoritative"))
	float RicochetAngleValidationTolerance;

	/** Max distance (cm) a hit actor can be from the claimed hit location for validation to pass. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Authoritative",
		meta = (ClampMin = "0",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientAuthoritative"))
	float ClientAuthHitLocationTolerance;

	/** Seconds of target motion the hit-location tolerance grows by (tolerance += target speed *
	 *  this). Covers client-side interpolation/smoothing lag plus network delay: the shooter aims
	 *  at a rendered pose that trails the server actor in proportion to target speed. 0 = flat
	 *  tolerance. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Authoritative",
		meta = (ClampMin = "0", ClampMax = "1",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientAuthoritative"))
	float ClientAuthTargetVelocityLagSeconds;

	/** Maximum amount a client-authoritative timestamp may lead server time. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Authoritative",
		meta = (ClampMin = "0", ClampMax = "0.5",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientAuthoritative"))
	float ClientAuthFutureTimestampTolerance;

	/** Position tolerance for client-authoritative trajectory plausibility checks. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Authoritative",
		meta = (ClampMin = "1", ClampMax = "5000",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientAuthoritative"))
	float ClientAuthTrajectoryTolerance;

	/** Fractional tolerance applied when comparing reported and server-evaluated speed. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority|Client Authoritative",
		meta = (ClampMin = "0", ClampMax = "1",
			EditCondition = "LaunchAuthorityMode == ELaunchAuthorityMode::ClientAuthoritative"))
	float ClientAuthSpeedTolerance;

	/** Hard admission ceiling for remote launch requests on one launcher component. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority",
		meta = (ClampMin = "1", ClampMax = "2048"))
	int32 MaxClientLaunchRequestsPerSecond;

	/** Lifetime used when GrantLaunchCredits receives no positive override. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Authority",
		meta = (ClampMin = "0.01", ClampMax = "30"))
	float DefaultLaunchCreditExpirySeconds;

	// Pooling

	/** Enable projectile object pooling. Reduces spawn/destroy overhead for high launch rates. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bEnableProjectilePooling;

	/** Number of projectile actors to pre-spawn per pool. Grows dynamically if exceeded. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "4", ClampMax = "512", EditCondition = "bEnableProjectilePooling"))
	int32 DefaultProjectilePoolSize;

	// Tick Batching

	/** Batch-tick all active projectiles through the aggregate ticker instead of individual component ticks. Better cache coherency for high projectile counts. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bEnableTickBatching;

	/** Custom aggregate tick class. Override to implement priority-based ticking, LOD, mobile optimization, etc. Leave null to use the default UBallisticsAggregateTick. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (EditCondition = "bEnableTickBatching"))
	TSubclassOf<UBallisticsAggregateTick> AggregateTickClass;

	// Impact Budget (VFX)

	/** Enable the impact VFX budget system. When enabled, impacts are throttled to prevent VFX overload in dense launchfights. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Impact Budget")
	bool bEnableImpactBudget;

	/** Maximum number of full-VFX impacts allowed per frame. Impacts beyond this are Reduced or Denied. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Impact Budget",
		meta = (ClampMin = "1", ClampMax = "256", EditCondition = "bEnableImpactBudget"))
	int32 MaxImpactsPerFrame;

	/** Maximum impacts a single projectile can generate (across its lifetime). 0 = unlimited. Useful for capping ricochet/penetration VFX chains. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Impact Budget",
		meta = (ClampMin = "0", ClampMax = "32", EditCondition = "bEnableImpactBudget"))
	int32 MaxImpactsPerProjectile;

	/** Distance (cm) from the camera beyond which impact priority is demoted by one level. Distant impacts are more likely to be culled. 0 = no distance culling. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Impact Budget",
		meta = (ClampMin = "0", EditCondition = "bEnableImpactBudget"))
	float ImpactBudgetFadeDistance;

	// Cosmetics routing

	/** Enable NPS built-in launch, fly-by, impact, and detonation playback and routing. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics")
	bool bEnableBuiltInCosmetics;

	/**
	 * Hard per-connection event budget for one frame. Additional unreliable events
	 * are discarded instead of allowing an unbounded network/VFX spike.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics",
		meta = (ClampMin = "1", ClampMax = "256", EditCondition = "bEnableBuiltInCosmetics"))
	int32 MaxCosmeticEventsPerConnectionPerFrame;

	/** Hard limit for remotely reconstructed sustained launch sequences per client. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics",
		meta = (ClampMin = "1", ClampMax = "128", EditCondition = "bEnableBuiltInCosmetics"))
	int32 MaxActiveLaunchSequencesPerClient;

	/** Maximum supported sustained-launch cosmetic rate. Values are clamped at runtime. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics",
		meta = (ClampMin = "1", ClampMax = "120", EditCondition = "bEnableBuiltInCosmetics"))
	float MaxLaunchCosmeticRate;

	/**
	 * Maximum cosmetic claims accepted from one client-owned launcher per second.
	 * Excess launch, impact, fly-by, detonation, and sequence-update claims are
	 * discarded on the server before they can create routing or playback work.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics",
		meta = (ClampMin = "1", ClampMax = "512", EditCondition = "bEnableBuiltInCosmetics"))
	int32 MaxClientCosmeticClaimsPerSecond;

	/**
	 * Absolute routing-distance ceiling in centimeters. Asset and Blueprint
	 * values are clamped again at runtime so malformed data cannot broadcast
	 * cosmetic events to an unintentionally large listener set.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics",
		meta = (ClampMin = "1000", ClampMax = "10000000", EditCondition = "bEnableBuiltInCosmetics"))
	float MaxCosmeticRoutingRadius;

	/**
	 * Maximum delayed cosmetic sounds retained by the aggregate queue in one world.
	 * New delayed sounds are dropped when the limit is reached.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics",
		meta = (ClampMin = "1", ClampMax = "2048", EditCondition = "bEnableBuiltInCosmetics"))
	int32 MaxPendingCosmeticAudioEvents;

	/**
	 * Hard limit for Niagara components retained by the aggregate cleanup queue.
	 * The oldest effect is stopped before a new effect can exceed this limit.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics",
		meta = (ClampMin = "1", ClampMax = "2048", EditCondition = "bEnableBuiltInCosmetics"))
	int32 MaxActiveCosmeticEffects;

	/** Clamp for distance-based audio delay so malformed data cannot retain events indefinitely. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics",
		meta = (ClampMin = "0", ClampMax = "30", EditCondition = "bEnableBuiltInCosmetics"))
	float MaxCosmeticAudioDelay;

	/**
	 * Automatically register player pawns as fly-by listeners. This includes
	 * remote pawn proxies needed by client-authoritative projectiles.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance|Cosmetics")
	bool bAutoRegisterPlayerFlyByListeners;

	// Physics

	/** Global gravity override (cm/s^2). Negative = downward. Set to 0 for zero-G environments. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Physics")
	float GravityZ;

	/** Global air density (g/cm^3). 0.001225 = sea level Earth. Reduce for high altitude or thin atmospheres. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Physics",
		meta = (ClampMin = "0"))
	float AirDensity;

	// Sliding (Projectile Low-Energy Behavior)

	/** Default: when a projectile's response resolves to Embed but its speed is above MinSlidingSpeed,
	 *  let it slide along the surface instead of stopping instantly. Can be overridden per-projectile. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Physics|Sliding")
	bool bDefaultEnableSliding;

	/** Default minimum speed (cm/s) for sliding to kick in. Higher = projectile stops instantly more often. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Physics|Sliding",
		meta = (ClampMin = "0"))
	float DefaultMinSlidingSpeed;

	/** Default friction for sliding projectiles (0 = ice, 1 = sandpaper). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Physics|Sliding",
		meta = (ClampMin = "0", ClampMax = "2"))
	float DefaultSlidingFriction;

	// Defaults

	/** Project-wide impact cosmetics selected by physical surface. Projectile entries override matching surfaces. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	TMap<TEnumAsByte<EPhysicalSurface>, FBallisticsImpactCosmetic> DefaultImpactCosmeticsBySurface;

	/** Global impact cosmetic used when a projectile has no matching surface entry or authored fallback. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	FBallisticsImpactCosmetic DefaultImpactCosmetic;

	const FBallisticsImpactCosmetic& GetImpactCosmetic(
		TEnumAsByte<EPhysicalSurface> Surface) const
	{
		if (const FBallisticsImpactCosmetic* Found = DefaultImpactCosmeticsBySurface.Find(Surface))
		{
			return *Found;
		}
		return DefaultImpactCosmetic;
	}

	/** Global fallback material DA. Used when neither the projectile's MaterialOverride nor the projectile component's MaterialDataAsset is set. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults",
		meta = (AllowedClasses = "/Script/BallisticsCoreRuntime.BallisticMaterialDataAsset"))
	FSoftObjectPath DefaultMaterialDataAsset;

	/** Global fallback projectile class used when the active projectile has no ProjectileClass set. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults",
		meta = (MetaClass = "/Script/Engine.Actor"))
	FSoftClassPath DefaultProjectileClass;

	// Category path for Project Settings

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
	virtual FName GetSectionName() const override { return FName(TEXT("Networked Projectile System")); }
};
