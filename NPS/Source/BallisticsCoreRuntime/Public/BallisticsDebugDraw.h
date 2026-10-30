// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "BallisticsTypes.h"
#include "BallisticsHitscanLibrary.h"
#include "BallisticsExplosiveDataAsset.h"

BALLISTICSCORERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogNPS, Log, All);

#if !UE_BUILD_SHIPPING
	#define NPS_LOG(Verbosity, Format, ...) \
		do { \
			if (FBallisticsDebugDraw::IsProjectileDebugEnabled()) \
			{ \
				UE_LOG(LogNPS, Verbosity, Format, ##__VA_ARGS__); \
			} \
		} while(0)
#else
	#define NPS_LOG(Verbosity, Format, ...) do {} while(0)
#endif

#if !UE_BUILD_SHIPPING

/**
 * Static debug drawing utilities for the NPS plugin.
 * All methods are no-ops in shipping builds.
 *
 * Console commands (registered by the runtime module):
 *   NPS.Debug.Projectiles    - toggle projectile path drawing
 *   NPS.Debug.Hitscan        - toggle hitscan trace drawing
 *   NPS.Debug.Detonation     - toggle blast radius and damage drawing
 *   NPS.Debug.Explosions     - alias for NPS.Debug.Detonation
 *   NPS.Debug.Budget         - toggle impact budget HUD
 *   NPS.Debug.All            - toggle everything
 *
 * Draw methods are available to project and plugin code.
 * They check the corresponding CVar and early-out if disabled.
 */
class BALLISTICSCORERUNTIME_API FBallisticsDebugDraw
{
public:
	/** Register all console commands. Called by the runtime module on startup. */
	static void RegisterConsoleCommands();

	// Projectile Path

	/** Draw a projectile's current position and velocity arrow */
	static void DrawProjectileState(
		const UWorld* World,
		const FVector& Position,
		const FVector& Velocity,
		EProjectileState State
	);

	/** Draw an impact marker at a hit point (color-coded by response type) */
	static void DrawImpactMarker(
		const UWorld* World,
		const FVector& Location,
		const FVector& Normal,
		EImpactResponse Response
	);

	/** Draw a line segment of a projectile's path */
	static void DrawPathSegment(
		const UWorld* World,
		const FVector& Start,
		const FVector& End,
		EProjectileState State
	);

	// Hitscan

	/** Draw a full hitscan result - all path segments and hit markers */
	static void DrawHitscanResult(
		const UWorld* World,
		const FHitscanResult& Result
	);

	// Explosions

	/** Draw blast radius spheres (inner + outer) and shrapnel traces */
	static void DrawDetonation(
		const UWorld* World,
		const FDetonationResult& Result,
		float BlastRadius,
		float BlastInnerRadius
	);

	/** Draw a single shrapnel trace line */
	static void DrawShrapnelTrace(
		const UWorld* World,
		const FVector& Origin,
		const FHitscanResult& TraceResult
	);

	// Budget HUD

	/** Draw on-screen impact budget stats. Call once per frame from the subsystem. */
	static void DrawBudgetHUD(
		const UWorld* World,
		int32 FrameImpactsUsed,
		int32 FrameBudgetMax
	);

	// CVar Queries

	static bool IsProjectileDebugEnabled();
	static bool IsHitscanDebugEnabled();
	static bool IsDetonationDebugEnabled();
	static bool IsExplosionDebugEnabled() { return IsDetonationDebugEnabled(); }
	static bool IsBudgetDebugEnabled();
};

#else

// Shipping build - everything compiles to nothing
class BALLISTICSCORERUNTIME_API FBallisticsDebugDraw
{
public:
	static void RegisterConsoleCommands() {}

	static void DrawProjectileState(const UWorld*, const FVector&, const FVector&, EProjectileState) {}
	static void DrawImpactMarker(const UWorld*, const FVector&, const FVector&, EImpactResponse) {}
	static void DrawPathSegment(const UWorld*, const FVector&, const FVector&, EProjectileState) {}
	static void DrawHitscanResult(const UWorld*, const FHitscanResult&) {}
	static void DrawDetonation(const UWorld*, const FDetonationResult&, float, float) {}
	static void DrawShrapnelTrace(const UWorld*, const FVector&, const FHitscanResult&) {}
	static void DrawBudgetHUD(const UWorld*, int32, int32) {}

	static bool IsProjectileDebugEnabled() { return false; }
	static bool IsHitscanDebugEnabled() { return false; }
	static bool IsDetonationDebugEnabled() { return false; }
	static bool IsExplosionDebugEnabled() { return false; }
	static bool IsBudgetDebugEnabled() { return false; }
};

#endif
