// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BallisticsTypes.h"
#include "BallisticsCosmeticTypes.h"
#include "BallisticsHitscanLibrary.h"
#include "BallisticsCoreSettings.h"
#include "ProjectileLauncherComponent.generated.h"

class UProjectileBallisticsComponent;
class UBallisticsProjectileDataAsset;
class UBallisticsExplosiveDataAsset;
class UProjectileCosmeticsManager;
class UBallisticsLaunchContributorComponent;
class USceneComponent;
class APawn;
class APlayerController;

struct FBallisticsLaunchCreditBatch
{
	int32 Remaining = 0;
	double ExpiryTime = 0.0;
};

struct FBallisticsRemoteLaunchAdmission
{
	int32 MemberCount = 0;
	TBitArray<> AcceptedMembers;
	double CommitTime = 0.0;
};

USTRUCT()
struct FBallisticsClientAuthoritativeLaunchSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UBallisticsProjectileDataAsset> ProjectileDataAsset;

	UPROPERTY()
	FProjectileInitialConditions CanonicalConditions;

	UPROPERTY()
	float ServerReceiveTime = 0.f;

	UPROPERTY()
	TArray<FProjectileFlightSegment> AcceptedSegments;

	UPROPERTY()
	int32 NextExpectedReportSequence = 0;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsLaunchEffectSourceBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Cosmetics")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Cosmetics")
	FComponentReference SourceComponent;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsAuthoritativeImpactEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	FHitResult HitResult;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	EImpactResponse ImpactResponse = EImpactResponse::None;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	int32 ProjectileID = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	TObjectPtr<UBallisticsProjectileDataAsset> ProjectileDataAsset;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	TObjectPtr<AActor> Instigator;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	TObjectPtr<AActor> ProjectileActor;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	TObjectPtr<AActor> HitActor;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	bool bValidatedClientReport = false;
};

USTRUCT(BlueprintType)
struct BALLISTICSCORERUNTIME_API FBallisticsAuthoritativeFlyByEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	TObjectPtr<APawn> Listener;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	FVector ClosestPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	float MissDistance = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	float ProjectileSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	TObjectPtr<UBallisticsProjectileDataAsset> ProjectileDataAsset;

	UPROPERTY(BlueprintReadOnly, Category = "Ballistics|Events")
	TObjectPtr<AActor> Instigator;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnBallisticsAuthoritativeImpact,
	const FBallisticsAuthoritativeImpactEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnBallisticsAuthoritativeFlyBy,
	const FBallisticsAuthoritativeFlyByEvent&, Event);

/** Launches projectile actors or hitscan traces through the selected authority mode. */
UCLASS(ClassGroup = (Ballistics), meta = (BlueprintSpawnableComponent))
class BALLISTICSCORERUNTIME_API UProjectileLauncherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	FGuid GetAudioSourceID() const { return AudioSourceID; }
	UProjectileLauncherComponent();

	// Configuration

	/** Active projectile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated,
		Category = "Ballistics|Projectile")
	TObjectPtr<UBallisticsProjectileDataAsset> ProjectileDataAsset;

	/** Server/standalone-only change for subsequent launches. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Projectile")
	bool SetProjectileDataAsset(UBallisticsProjectileDataAsset* NewProjectileData);

	/** Get the active projectile. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Projectile")
	UBallisticsProjectileDataAsset* GetProjectileDataAsset() const { return ProjectileDataAsset; }

	/** Minimum seconds per accepted remote launch. Uses a three-launch burst bucket. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Ballistics|Launcher",
		meta = (ClampMin = "0"))
	float MinLaunchInterval = 0.f;

	/** Require server-granted credits for remote launches. Disabled preserves unlimited launches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Ballistics|Launcher")
	bool bRequireLaunchCredits = false;

	/** Authorize further remote launches. Server-only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ballistics|Launcher")
	void GrantLaunchCredits(int32 Count, float ExpirySeconds = 0.f);

	/** Return the number of currently unexpired server launch credits. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Launcher")
	int32 GetAvailableLaunchCredits() const;

	/** Remove every outstanding launch credit. Server-only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Ballistics|Launcher")
	void ClearLaunchCredits();

	// Launch cosmetics

	/** Base launch audio and VFX. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_LaunchCosmeticProfile,
		Category = "Ballistics|Cosmetics")
	TObjectPtr<UBallisticsLaunchCosmeticProfile> LaunchCosmeticProfile;

	/** Component that owns the sockets named by launch effect entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Cosmetics")
	FComponentReference LaunchEffectSourceComponent;

	/** Named component sources selected by each launch effect's SourceBinding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Cosmetics",
		meta = (TitleProperty = "Name"))
	TArray<FBallisticsLaunchEffectSourceBinding> LaunchEffectSourceBindings;

	/** Enable automatic launch/impact/fly-by/detonation routing for this launcher. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Cosmetics")
	bool bEnableCosmeticRouting = true;

	/**
	 * Route launch events even when no built-in profile asset is assigned.
	 * Leave disabled to prevent empty events. Enable only when consuming the
	 * manager's OnCosmeticEvent extension point with project-specific playback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Cosmetics")
	bool bRouteCosmeticEventsWithoutAssets = false;

	/** Listener radius used only for asset-free custom cosmetic events. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ballistics|Cosmetics",
		meta = (ClampMin = "0", EditCondition = "bRouteCosmeticEventsWithoutAssets"))
	float CustomCosmeticRoutingRadius = 200000.f;

	/** Server/standalone-only setter. Returns false instead of accepting a client-side authority change. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Cosmetics")
	bool SetLaunchCosmeticProfile(UBallisticsLaunchCosmeticProfile* NewProfile);

	UFUNCTION(BlueprintCallable, Category = "Ballistics|Cosmetics")
	void SetLaunchEffectSourceComponent(USceneComponent* NewSourceComponent);

	/** Add, replace, or clear a named launch effect source. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Cosmetics")
	bool SetLaunchEffectSourceBinding(FName Name, USceneComponent* NewSourceComponent);

	/** Resolve the default source when Name is None, or an exact named source. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Cosmetics")
	USceneComponent* GetLaunchEffectSourceComponent(FName Name) const;

	/** Resolve a contributor-aware source component and socket to world space. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Launcher")
	bool ResolveLaunchSourceTransform(
		FName SourceBinding,
		FName SocketName,
		FTransform& OutTransform) const;

	void RegisterLaunchContributor(UBallisticsLaunchContributorComponent* Contributor);
	void UnregisterLaunchContributor(UBallisticsLaunchContributorComponent* Contributor);
	void NotifyLaunchContributorChanged(UBallisticsLaunchContributorComponent* Contributor);

	const FResolvedLaunchCosmetics& GetResolvedLaunchCosmetics() const;
	FGuid GetPreparedLaunchCosmeticKey() const;
	const TArray<TObjectPtr<UBallisticsLaunchCosmeticModifier>>& GetActiveLaunchModifiers() const;

	/**
	 * Begin remote reconstruction for sustained launch effects.
	 * Continue calling Launch for actual projectiles; call EndLaunchSequence when launching stops.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Cosmetics")
	void BeginLaunchSequence(
		const FVector& Origin,
		const FVector& Direction,
		float LaunchesPerSecond);

	UFUNCTION(BlueprintCallable, Category = "Ballistics|Cosmetics")
	void EndLaunchSequence();

	UFUNCTION(BlueprintPure, Category = "Ballistics|Cosmetics")
	bool IsLaunchSequenceActive() const { return bLaunchSequenceActive; }

	/** Internal automatic bridge used by projectiles created by this launcher. */
	void HandleProjectileImpactCosmetic(
		const FHitResult& Hit,
		EImpactResponse Response,
		int32 ProjectileID,
		UBallisticsProjectileDataAsset* SourceProjectileData);
	void HandleProjectileFlyByCosmetic(
		APawn* NearbyPawn,
		const FVector& ClosestPoint,
		float MissDistance,
		float ProjectileSpeed,
		const FVector& ProjectileDirection,
		UBallisticsProjectileDataAsset* SourceProjectileData);
	void HandleDetonationCosmetic(
		const FVector& Location,
		UBallisticsExplosiveDataAsset* ExplosiveData);

	// Launching

	/** Launch using the active projectile and authority mode. */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Launcher")
	void Launch(const FVector& Origin, const FVector& Direction);

	/** Get the active authority mode. */
	UFUNCTION(BlueprintPure, Category = "Ballistics|Launcher")
	ELaunchAuthorityMode GetActiveAuthorityMode() const;

	// Client Authoritative: Submit Flight Log

	/**
	 * Called by the client when a projectile hits something in Client Authoritative mode.
	 * Builds and sends the full launch path to the server for validation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ballistics|Launcher")
	void SubmitFlightLog(const FProjectileFlightLog& FlightLog);

	// Server RPCs

	/** ServerAuthoritative: client requests the server to launch */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestLaunch(
		FVector_NetQuantize10 Origin,
		FVector_NetQuantizeNormal Direction,
		int32 RandomSeed,
		int32 ProjectileID,
		int32 LaunchRequestID,
		int32 LaunchMemberIndex,
		int32 LaunchMemberCount);

	/** ClientPredicted: client tells server it launched locally, server spawns + fast-forwards */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerNotifyLaunch(
		FVector_NetQuantize10 Origin,
		FVector_NetQuantizeNormal Direction,
		int32 RandomSeed,
		float ClientTimestamp,
		int32 ProjectileID,
		int32 LaunchRequestID,
		int32 LaunchMemberIndex,
		int32 LaunchMemberCount);

	/** ClientPredicted: client claims a hit, server validates */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCheckClientHit(AActor* HitActor, float ClaimTimestamp, FVector ClaimPosition);

	/** ClientAuthoritative: client submits a cumulative flight report for validation */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSubmitFlightLog(FProjectileFlightLog FlightLog);

	/** Records the server-owned projectile configuration for a client-authoritative launch. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRegisterClientAuthoritativeLaunch(
		int32 ProjectileID,
		FVector_NetQuantize10 Origin,
		FVector_NetQuantizeNormal Direction,
		int32 RandomSeed,
		float ClientTimestamp,
		int32 LaunchRequestID,
		int32 LaunchMemberIndex,
		int32 LaunchMemberCount);

	/** One-time launch cosmetic claim. Sustained launch sequences use the aggregate sequence RPCs. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerNotifyLaunchCosmetic(FVector_NetQuantize10 Origin, FVector_NetQuantizeNormal Direction);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerBeginLaunchSequence(
		FVector_NetQuantize10 Origin,
		FVector_NetQuantizeNormal Direction,
		float LaunchesPerSecond,
		int32 SequenceID);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerEndLaunchSequence(int32 SequenceID);

	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerUpdateLaunchSequence(
		FVector_NetQuantize10 Origin,
		FVector_NetQuantizeNormal Direction,
		int32 SequenceID);

	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerNotifyImpactCosmetic(
		FVector_NetQuantize10 Location,
		FVector_NetQuantizeNormal Normal,
		EPhysicalSurface SurfaceType,
		uint8 Response,
		int32 ProjectileID);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerNotifyFlyByCosmetic(
		FVector_NetQuantize10 ClosestPoint,
		FVector_NetQuantizeNormal ProjectileDirection,
		float MissDistance,
		float ProjectileSpeed);

	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerNotifyDetonationCosmetic(FVector_NetQuantize10 Location);

	// Client RPCs

	/** Server tells client to reconcile - the predicted projectile was too far off (Client Predicted) */
	UFUNCTION(Client, Reliable)
	void ClientReconcileProjectile(int32 ProjectileID, FVector CorrectedPosition, FVector CorrectedVelocity, float ServerElapsedTime);

	/** Server tells clients to spawn a cosmetic for a non-replicating server-authoritative projectile. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastOnProjectileLaunched(FProjectileInitialConditions Conditions, bool bClientInitiated);

	/** Server tells the client whether their flight log was accepted or rejected */
	UFUNCTION(Client, Reliable)
	void ClientFlightLogResult(int32 LaunchID, EProjectileFlightValidationResult Result, int32 FailedSegmentIndex);

	// Delegates

	/**
	 * Server callback for Client Predicted hit validation.
	 * Return true to confirm the hit, false to reject.
	 */
	FOnValidateHit OnValidateHit;

	/** Broadcasts after the server validates or rejects a hit claim (Client Predicted mode). */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnHitValidationResult OnHitValidationResult;

	/** Broadcasts after the server validates a client-authoritative flight log. */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnProjectileFlightLogValidated OnFlightLogValidated;

	/** Broadcasts when a hitscan trace completes. Bind to apply damage from hitscan hits. */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnHitscanComplete OnHitscanComplete;

	/** Server-only impact stream shared by every authority mode. */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnBallisticsAuthoritativeImpact OnAuthoritativeImpact;

	/** Server-only fly-by stream. */
	UPROPERTY(BlueprintAssignable, Category = "Ballistics|Events")
	FOnBallisticsAuthoritativeFlyBy OnAuthoritativeFlyBy;

#if WITH_DEV_AUTOMATION_TESTS
	void SubmitFlightLogForTests(const FProjectileFlightLog& FlightLog);
	void RegisterClientAuthoritativeLaunchForTests(
		int32 ProjectileID,
		const FProjectileInitialConditions& Conditions,
		float ClientTimestamp);
#endif

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Server-Authoritative Flow

	void Launch_ServerAuthoritative(
		const FVector& Origin,
		const FVector& Direction,
		int32 LaunchRequestID,
		int32 LaunchMemberIndex,
		int32 LaunchMemberCount);

	// Client-Predicted Flow

	void Launch_ClientPredicted(
		const FVector& Origin,
		const FVector& Direction,
		int32 LaunchRequestID,
		int32 LaunchMemberIndex,
		int32 LaunchMemberCount);

	// Client-Authoritative Flow

	void Launch_ClientAuthoritative(
		const FVector& Origin,
		const FVector& Direction,
		int32 LaunchRequestID,
		int32 LaunchMemberIndex,
		int32 LaunchMemberCount);

	// Hitscan

	/** Launch an instant hitscan trace through the material pipeline */
	void LaunchHitscan(const FVector& Origin, const FVector& Direction);

	// Spread / Projectile

	/** Launch a single projectile through the active authority flow */
	void LaunchSingle(
		const FVector& Origin,
		const FVector& Direction,
		int32 LaunchRequestID,
		int32 LaunchMemberIndex,
		int32 LaunchMemberCount);

	/** Launch multiple projectiles using the active projectile's spread config */
	void LaunchSpread(
		const FVector& Origin,
		const FVector& Direction,
		int32 LaunchRequestID);

	/** Build initial conditions from base + projectile data asset overrides */
	FProjectileInitialConditions BuildConditions(const FVector& Origin, const FVector& Direction);
	FProjectileInitialConditions BuildCanonicalServerConditions(
		const FVector& Origin,
		const FVector& Direction,
		int32 RandomSeed) const;
	bool ValidateRemoteLaunchRequest(
		ELaunchAuthorityMode AuthorityMode,
		const FProjectileInitialConditions& CanonicalConditions,
		int32 ProjectileID,
		float ClientTimestamp);
	bool TryAcceptRemoteLaunchRequest();
	bool CanAcceptRemoteLaunchAdmission(
		int32 ProjectileID,
		int32 LaunchRequestID,
		int32 LaunchMemberIndex,
		int32 LaunchMemberCount);
	bool CommitRemoteLaunchAdmission(
		int32 ProjectileID,
		int32 LaunchRequestID,
		int32 LaunchMemberIndex,
		int32 LaunchMemberCount);
	bool TryConsumeLocalLaunchCadence();
	bool IsRemoteClientLaunch() const;
	int32 GetExpectedLaunchMemberCount() const;
	double GetLaunchAdmissionTime() const;
	void RefreshLaunchAdmissionOwner();
	void PruneLaunchCredits(double Now);
	void PruneRemoteLaunchAdmissions(double Now);
	bool HasLaunchCredit(double Now) const;
	bool ConsumeLaunchCredit(double Now);
	void RefillCadenceTokens(double Now, double& LastRefillTime, float& Tokens) const;
	int32 GenerateLaunchRequestID();

	// Server-Side Flight Log Validation

	/** Ring buffer of saved flight logs per-player, processed by the server */
	TArray<FProjectileFlightLog> SavedFlightLogs;

	UPROPERTY()
	TMap<int32, FBallisticsClientAuthoritativeLaunchSnapshot> ClientAuthoritativeLaunchSnapshots;

	void PruneClientAuthoritativeLaunchSnapshots(float ServerTime);
	FBallisticsClientAuthoritativeLaunchSnapshot* FindClientAuthoritativeLaunchSnapshot(
		int32 ProjectileID);

	/** Add a flight log to the ring buffer, evicting oldest if full */
	void AddFlightLog(const FProjectileFlightLog& FlightLog);

	/** Validate a single flight log: timestamp, visibility, angles, materials */
	EProjectileFlightValidationResult ValidateFlightLog(
		const FProjectileFlightLog& FlightLog,
		const FBallisticsClientAuthoritativeLaunchSnapshot& LaunchSnapshot,
		int32 FirstSegmentIndex,
		int32& OutFailedSegmentIndex);

	/** Validate a single segment: line trace visibility check */
	bool ValidateSegmentVisibility(const FProjectileFlightSegment& Segment);

	/** Validate that a ricochet angle is physically plausible */
	bool ValidateRicochetAngle(const FProjectileFlightSegment& CurrentSegment, const FProjectileFlightSegment& NextSegment);

	/** Validate that the material at the impact point is actually penetrable */
	bool ValidatePenetration(
		const FProjectileFlightSegment& Segment,
		const UBallisticsProjectileDataAsset* LaunchProjectileData,
		const FProjectileInitialConditions& CanonicalConditions) const;

	// Shared

	/** Spawn a projectile actor and initialize its ballistics component */
	AActor* SpawnProjectile(const FProjectileInitialConditions& Conditions);

	/** Generate a deterministic seed from launch counter + owner ID */
	int32 GenerateSeed();

	void DispatchLaunchCosmetic(const FVector& Origin, const FVector& Direction);
	void QueueLaunchCosmetic(
		const FVector& Origin,
		const FVector& Direction,
		APlayerController* ExcludedController);
	void QueueSequenceTransform(
		const FVector& Origin,
		const FVector& Direction,
		APlayerController* ExcludedController);
	FProjectileCosmeticEvent BuildLaunchCosmeticEvent(
		const FVector& Origin,
		const FVector& Direction);
	UProjectileCosmeticsManager* GetCosmeticsManager() const;
	APlayerController* GetOwningPlayerController() const;
	bool IsLocallyControlledLauncher() const;
	bool HasConfiguredLaunchCosmetics() const;
	float GetLaunchCosmeticRoutingRadius() const;
	bool ResolveLaunchEffectTransform(
		const FBallisticsLaunchEffect& Effect,
		const FVector& FallbackOrigin,
		const FVector& Direction,
		FTransform& OutTransform) const;
	bool ResolveLaunchEffectSource(
		FName Name,
		USceneComponent*& OutComponent,
		FName& OutSocketOverride) const;
	void RebuildLaunchContributorCache() const;
	APawn* ResolveLaunchingPawn() const;
	float GetSynchronizedTime() const;
	int32 GenerateCosmeticEventID();
	int32 GenerateSequenceID();
	bool ValidateCosmeticVector(const FVector& Vector) const;
	bool ShouldExcludeOwningClientFromImpactRelay(
		const UBallisticsProjectileDataAsset* SourceProjectileData) const;

	friend class UBallisticsDetonationComponent;

	UPROPERTY()
	int32 LaunchCounter;

	UPROPERTY()
	int32 NextProjectileID;

	UPROPERTY()
	int32 NextLaunchRequestID;

	UPROPERTY()
	bool bLaunchSequenceActive = false;

	UPROPERTY()
	int32 ActiveLaunchSequenceID = INDEX_NONE;

	UPROPERTY()
	float ActiveLaunchesPerSecond = 0.f;

	UPROPERTY()
	int32 CosmeticEventCounter = 0;
	FGuid AudioSourceID;

	UPROPERTY()
	int32 SequenceCounter = 0;

	float LastSequenceTransformSendTime = -BIG_NUMBER;
	float ClientCosmeticClaimWindowStart = -BIG_NUMBER;
	float LastClientCosmeticClaimWarningTime = -BIG_NUMBER;
	int32 ClientCosmeticClaimsThisWindow = 0;
	float RemoteLaunchRequestWindowStart = -BIG_NUMBER;
	float LastRemoteLaunchRequestWarningTime = -BIG_NUMBER;
	int32 RemoteLaunchRequestsThisWindow = 0;
	double ServerCadenceLastRefillTime = -1.0;
	float ServerCadenceTokens = 3.f;
	double ClientCadenceLastRefillTime = -1.0;
	float ClientCadenceTokens = 3.f;
	TArray<FBallisticsLaunchCreditBatch> LaunchCreditBatches;
	TMap<int32, FBallisticsRemoteLaunchAdmission> RemoteLaunchAdmissions;
	TMap<int32, double> RecentRemoteProjectileIDs;
	TWeakObjectPtr<AActor> CachedLaunchAdmissionOwner;
	TWeakObjectPtr<APlayerController> CachedLaunchAdmissionController;
	bool bLaunchAdmissionOwnerInitialized = false;

	TArray<TWeakObjectPtr<UBallisticsLaunchContributorComponent>> LaunchContributors;
	mutable TArray<TWeakObjectPtr<UBallisticsLaunchContributorComponent>> SortedLaunchContributors;
	mutable TArray<TObjectPtr<UBallisticsLaunchCosmeticModifier>> CachedLaunchModifiers;
	mutable FResolvedLaunchCosmetics CachedResolvedLaunchCosmetics;
	mutable TSharedPtr<const FProjectileCosmeticStaticTemplate> CachedLaunchWireTemplate;
	struct FCachedAuxiliaryWireTemplate
	{
		TWeakObjectPtr<const UObject> Asset;
		TSharedPtr<const FProjectileCosmeticStaticTemplate> Template;
	};
	TMap<uint64, FCachedAuxiliaryWireTemplate> AuxiliaryWireTemplates;
	void PrepareAuxiliaryWireTemplate(FProjectileCosmeticEvent& Event, const UObject* Asset, uint32 Variant);
	mutable bool bLaunchContributorCacheDirty = true;
	mutable bool bWarnedLaunchContributorBudget = false;

	/** Server-side admission control for cosmetic-only client RPCs. */
	bool TryAcceptClientCosmeticClaim(const TCHAR* ClaimType);

	UFUNCTION()
	void OnRep_LaunchCosmeticProfile();

	/** Track predicted projectiles for reconciliation (ClientPredicted mode) */
	UPROPERTY()
	TMap<int32, TWeakObjectPtr<AActor>> PredictedProjectiles;

	/** Per-projectile conditions produced by the spread asset for the current multi-projectile launch. */
	FProjectileInitialConditions ProjectileConditionsOverride;
	bool bUseProjectileConditions = false;
};
