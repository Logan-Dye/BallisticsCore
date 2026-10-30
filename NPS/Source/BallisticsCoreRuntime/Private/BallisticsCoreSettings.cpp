// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsCoreSettings.h"

UBallisticsCoreSettings::UBallisticsCoreSettings()
{
	// Authority
	LaunchAuthorityMode = ELaunchAuthorityMode::ClientPredicted;
	ServerAuthCosmeticMode = EServerAuthCosmeticMode::ImmediateLocalCosmetic;

	// Client Predicted
	MaxLagCompensationWindow = 0.25f;
	PredictionReconciliationTolerance = 10.f;

	// Client Authoritative
	MaxSavedFlightLogs = 96;
	MaxFlightLogAge = 2.0f;
	RicochetAngleValidationTolerance = 10.f;
	ClientAuthHitLocationTolerance = 100.f;
	ClientAuthTargetVelocityLagSeconds = 0.35f;
	ClientAuthFutureTimestampTolerance = 0.05f;
	ClientAuthTrajectoryTolerance = 500.f;
	ClientAuthSpeedTolerance = 0.1f;
	MaxClientLaunchRequestsPerSecond = 256;
	DefaultLaunchCreditExpirySeconds = 1.0f;

	// Performance
	bEnableProjectilePooling = true;
	DefaultProjectilePoolSize = 64;
	bEnableTickBatching = true;

	// Impact Budget
	bEnableImpactBudget = true;
	MaxImpactsPerFrame = 32;
	MaxImpactsPerProjectile = 8;
	ImpactBudgetFadeDistance = 30000.f; // 300 meters

	// Cosmetics
	bEnableBuiltInCosmetics = true;
	MaxCosmeticEventsPerConnectionPerFrame = 64;
	MaxActiveLaunchSequencesPerClient = 32;
	MaxLaunchCosmeticRate = 60.f;
	MaxClientCosmeticClaimsPerSecond = 256;
	MaxCosmeticRoutingRadius = 2000000.f;
	MaxPendingCosmeticAudioEvents = 256;
	MaxActiveCosmeticEffects = 256;
	MaxCosmeticAudioDelay = 15.f;
	bAutoRegisterPlayerFlyByListeners = true;

	// Physics
	GravityZ = -980.f;
	AirDensity = 0.001225f;

	// Sliding defaults
	bDefaultEnableSliding = true;
	DefaultMinSlidingSpeed = 50.f;
	DefaultSlidingFriction = 0.5f;
}

const UBallisticsCoreSettings* UBallisticsCoreSettings::Get()
{
	return GetDefault<UBallisticsCoreSettings>();
}
