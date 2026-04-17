// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "BallisticsCoreSettings.h"

UBallisticsCoreSettings::UBallisticsCoreSettings()
{
	FireAuthorityMode = EFireAuthorityMode::ClientPredicted;
	MaxLagCompensationWindow = 0.25f;
	PredictionReconciliationTolerance = 10.f;
	bEnableProjectilePooling = true;
	DefaultProjectilePoolSize = 64;
	bEnableTickBatching = false;
	GravityZ = -980.f;
	AirDensity = 0.001225f;
}

const UBallisticsCoreSettings* UBallisticsCoreSettings::Get()
{
	return GetDefault<UBallisticsCoreSettings>();
}
