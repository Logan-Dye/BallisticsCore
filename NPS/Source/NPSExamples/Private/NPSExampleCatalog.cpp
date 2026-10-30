// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "NPSExampleCatalog.h"

#include "BallisticsCosmeticTypes.h"
#include "BallisticsExplosiveDataAsset.h"
#include "BallisticsProjectileDataAsset.h"
#include "BallisticsSpreadDataAsset.h"

#include <initializer_list>

namespace NPSExampleCatalog
{
FNPSExampleFact Fact(const TCHAR* Label, const TCHAR* Value)
{
	FNPSExampleFact Result;
	Result.Label = FText::FromString(Label);
	Result.Value = FText::FromString(Value);
	return Result;
}

FNPSExampleMapEntry Map(const TCHAR* Id, const TCHAR* Title, const TCHAR* Summary,
						const TCHAR* Path, ENPSExampleScenario Scenario)
{
	FNPSExampleMapEntry Result;
	Result.MapId = FName(Id);
	Result.Title = FText::FromString(Title);
	Result.Summary = FText::FromString(Summary);
	Result.Map = TSoftObjectPtr<UWorld>(FSoftObjectPath(Path));
	Result.Scenario = Scenario;
	return Result;
}

FNPSExampleStationEntry Station(const TCHAR* Id, const TCHAR* Title, const TCHAR* Summary,
								const TCHAR* Expected, ENPSExampleScenario Scenario,
								const TCHAR* ProjectilePath,
								std::initializer_list<FNPSExampleFact> Facts = {})
{
	FNPSExampleStationEntry Result;
	Result.ExampleId = FName(Id);
	Result.Title = FText::FromString(Title);
	Result.Summary = FText::FromString(Summary);
	Result.ExpectedResult = FText::FromString(Expected);
	Result.Scenario = Scenario;
	Result.ProjectileData =
		TSoftObjectPtr<UBallisticsProjectileDataAsset>(FSoftObjectPath(ProjectilePath));
	for (const FNPSExampleFact& Entry : Facts)
	{
		Result.Facts.Add(Entry);
	}
	return Result;
}
} // namespace NPSExampleCatalog

const FNPSExampleStationEntry* UNPSExampleCatalog::FindStation(FName ExampleId) const
{
	for (const FNPSExampleStationEntry& Entry : Stations)
	{
		if (Entry.ExampleId == ExampleId)
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FNPSExampleStationEntry* UNPSExampleCatalog::FindStationByTitle(const FString& Title) const
{
	return Stations.FindByPredicate(
		[&Title](const FNPSExampleStationEntry& Entry)
		{ return Entry.Title.ToString().Equals(Title, ESearchCase::IgnoreCase); });
}

UNPSExampleCatalog* UNPSExampleCatalog::LoadDefaultCatalog()
{
	return LoadObject<UNPSExampleCatalog>(
		nullptr, TEXT("/NPS/Examples/Data/DA_NPS_ExampleCatalog.DA_NPS_ExampleCatalog"));
}

void UNPSExampleCatalog::BuildDefaultContent(TArray<FNPSExampleMapEntry>& OutMaps,
											 TArray<FNPSExampleStationEntry>& OutStations)
{
	using namespace NPSExampleCatalog;
	OutMaps = {
		Map(TEXT("playground"), TEXT("First Person Playground"),
			TEXT("Playable rifle, pistol, and launcher with runtime authority, explosive, wind, "
				 "and debug controls."),
			TEXT("/NPS/Examples/Maps/NPS_Playground.NPS_Playground"),
			ENPSExampleScenario::Showcase),
		Map(TEXT("showcase"), TEXT("Complete Showcase"), TEXT("All automated stations in one map."),
			TEXT("/NPS/Examples/Maps/NPS_Showcase.NPS_Showcase"), ENPSExampleScenario::Showcase),
		Map(TEXT("authority"), TEXT("Authority Modes"),
			TEXT("Side-by-side server-authoritative, client-predicted, and client-authoritative "
				 "launch behavior."),
			TEXT("/NPS/Examples/Maps/NPS_AuthorityModes.NPS_AuthorityModes"),
			ENPSExampleScenario::AuthorityModes),
		Map(TEXT("trajectories"), TEXT("Trajectories"),
			TEXT("Velocity, drag, gravity, and wind response."),
			TEXT("/NPS/Examples/Maps/NPS_Trajectories.NPS_Trajectories"),
			ENPSExampleScenario::Trajectories),
		Map(TEXT("materials"), TEXT("Material Responses"),
			TEXT("Penetration, ricochet, embed, and slide outcomes."),
			TEXT("/NPS/Examples/Maps/NPS_MaterialResponses.NPS_MaterialResponses"),
			ENPSExampleScenario::MaterialResponses),
		Map(TEXT("multi_projectile"), TEXT("Hitscan and Spread"),
			TEXT("Hitscan, random cone, and authored fixed patterns."),
			TEXT("/NPS/Examples/Maps/NPS_MultiProjectile.NPS_MultiProjectile"),
			ENPSExampleScenario::MultiProjectile),
		Map(TEXT("explosives"), TEXT("Explosives"),
			TEXT("Impact, fuse, rest, and manual detonation triggers."),
			TEXT("/NPS/Examples/Maps/NPS_Explosives.NPS_Explosives"),
			ENPSExampleScenario::Explosives),
		Map(TEXT("cosmetics"), TEXT("Launch Cosmetics"),
			TEXT("Profiles, attachment modifiers, sustained sequences, and distance routing."),
			TEXT("/NPS/Examples/Maps/NPS_Cosmetics.NPS_Cosmetics"), ENPSExampleScenario::Cosmetics),
		Map(TEXT("network_scale"), TEXT("Network Scale"),
			TEXT("Cosmetic delivery beyond actor relevancy distance."),
			TEXT("/NPS/Examples/Maps/NPS_NetworkScale.NPS_NetworkScale"),
			ENPSExampleScenario::NetworkScale),
		Map(TEXT("performance"), TEXT("Pooling and Load"),
			TEXT("A pooled 32-projectile spread workload using the aggregate simulation path."),
			TEXT("/NPS/Examples/Maps/NPS_Performance.NPS_Performance"),
			ENPSExampleScenario::Performance)};

	OutStations.Reset();
	auto Add = [&OutStations](FNPSExampleStationEntry&& Entry)
	{ OutStations.Add(MoveTemp(Entry)); };
	Add(Station(TEXT("server_authoritative"), TEXT("SERVER AUTHORITATIVE"),
				TEXT("The server owns simulation and damage."),
				TEXT("All clients converge on the server projectile and impact."),
				ENPSExampleScenario::AuthorityModes,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ServerAuth.DA_NPS_ServerAuth"),
				{Fact(TEXT("Authority"), TEXT("ServerAuthoritative"))}));
	Add(Station(
		TEXT("client_predicted"), TEXT("CLIENT PREDICTED"),
		TEXT("The owning client predicts while the server validates and replicates."),
		TEXT("The owner receives immediate presentation and later correction if required."),
		ENPSExampleScenario::AuthorityModes,
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ClientPredicted.DA_NPS_ClientPredicted"),
		{Fact(TEXT("Authority"), TEXT("ClientPredicted"))}));
	Add(Station(
		TEXT("client_authoritative"), TEXT("CLIENT AUTHORITATIVE"),
		TEXT("The client simulates and submits a bounded flight log for server validation."),
		TEXT("Validated segments drive authoritative damage and remote presentation."),
		ENPSExampleScenario::AuthorityModes,
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ClientAuth.DA_NPS_ClientAuth"),
		{Fact(TEXT("Authority"), TEXT("ClientAuthoritative"))}));
	Add(Station(TEXT("high_velocity"), TEXT("HIGH VELOCITY PROJECTILE"),
				TEXT("A fast, low-drag projectile demonstrates a flat trajectory."),
				TEXT("The path stays nearly level before impact."),
				ENPSExampleScenario::Trajectories,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity"),
				{Fact(TEXT("Initial speed"), TEXT("900 m/s"))}));
	Add(Station(TEXT("subsonic"), TEXT("SUBSONIC HEAVY"),
				TEXT("A slower, heavier projectile exposes drag and gravity over distance."),
				TEXT("The path drops sooner than the high-velocity lane."),
				ENPSExampleScenario::Trajectories,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Subsonic.DA_NPS_Subsonic"),
				{Fact(TEXT("Initial speed"), TEXT("300 m/s"))}));
	Add(Station(TEXT("gravity_arc"), TEXT("ARROW - GRAVITY ARC"),
				TEXT("A low initial velocity and high drag make gravity easy to inspect."),
				TEXT("The projectile follows a visible arc into the target."),
				ENPSExampleScenario::Trajectories,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Arrow.DA_NPS_Arrow"),
				{Fact(TEXT("Launch pitch"), TEXT("8 degrees"))}));
	Add(Station(TEXT("crosswind"), TEXT("CROSSWIND DRIFT"),
				TEXT("Constant positive-Y acceleration demonstrates environmental wind influence."),
				TEXT("The path bends toward positive Y before reaching the target distance."),
				ENPSExampleScenario::Trajectories,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Crosswind.DA_NPS_Crosswind"),
				{Fact(TEXT("Wind"), TEXT("+Y 6000 cm/s2"))}));
	Add(Station(TEXT("penetration"), TEXT("PENETRATION"),
				TEXT("A high-energy projectile uses a penetrable material response."),
				TEXT("The trace continues through the target with reduced energy."),
				ENPSExampleScenario::MaterialResponses,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Penetrator.DA_NPS_Penetrator")));
	Add(Station(TEXT("ricochet"), TEXT("RICOCHET"),
				TEXT("A shallow impact angle uses the material ricochet response."),
				TEXT("The projectile changes direction instead of stopping."),
				ENPSExampleScenario::MaterialResponses,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Ricochet.DA_NPS_Ricochet"),
				{Fact(TEXT("Launch pitch"), TEXT("-7 degrees"))}));
	Add(Station(TEXT("embed"), TEXT("EMBED / SLIDE"),
				TEXT("A non-ricocheting projectile demonstrates terminal surface behavior."),
				TEXT("The projectile embeds or transitions into the configured slide response."),
				ENPSExampleScenario::MaterialResponses,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Arrow.DA_NPS_Arrow")));
	Add(Station(
		TEXT("hitscan"), TEXT("HITSCAN"),
		TEXT("The same launch API resolves the path immediately without a projectile actor."),
		TEXT("The target registers an immediate hit and material response."),
		ENPSExampleScenario::MultiProjectile,
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Hitscan.DA_NPS_Hitscan"),
		{Fact(TEXT("Mode"), TEXT("Hitscan"))}));
	Add(Station(TEXT("random_spread"), TEXT("RANDOM CONE SPREAD"),
				TEXT("Each launch samples a deterministic direction inside the configured cone."),
				TEXT("Repeated cycles form a distributed cone around the forward axis."),
				ENPSExampleScenario::MultiProjectile,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_SpreadRandom.DA_NPS_SpreadRandom")));
	Add(Station(TEXT("fixed_spread"), TEXT("FIXED PATTERN SPREAD"),
				TEXT("An authored 2D pattern produces repeatable projectile offsets."),
				TEXT("Every cycle reproduces the same visible pattern."),
				ENPSExampleScenario::MultiProjectile,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_SpreadPattern.DA_NPS_SpreadPattern")));
	Add(Station(
		TEXT("impact_detonation"), TEXT("IMPACT DETONATION"),
		TEXT("The explosive payload detonates on its first blocking impact."),
		TEXT("Blast and fragment damage originate at the impact point."),
		ENPSExampleScenario::Explosives,
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveImpact.DA_NPS_ExplosiveImpact"),
		{Fact(TEXT("Trigger"), TEXT("OnImpact"))}));
	Add(Station(TEXT("fuse"), TEXT("FUSE TIMER"),
				TEXT("The explosive arms at launch and detonates after its configured fuse."),
				TEXT("Detonation occurs in flight after 0.65 seconds."),
				ENPSExampleScenario::Explosives,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveFuse.DA_NPS_ExplosiveFuse"),
				{Fact(TEXT("Fuse"), TEXT("0.65 seconds"))}));
	Add(Station(TEXT("rest"), TEXT("DETONATE ON REST"),
				TEXT("The payload waits for the projectile to settle."),
				TEXT("Detonation follows the transition to rest."), ENPSExampleScenario::Explosives,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveRest.DA_NPS_ExplosiveRest"),
				{Fact(TEXT("Trigger"), TEXT("OnRest"))}));
	Add(Station(
		TEXT("manual"), TEXT("MANUAL DETONATION"),
		TEXT("Gameplay calls Detonate after launch through the aggregate example sequence."),
		TEXT("The payload detonates about 0.35 seconds after launch."),
		ENPSExampleScenario::Explosives,
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_ExplosiveManual.DA_NPS_ExplosiveManual"),
		{Fact(TEXT("Trigger"), TEXT("Manual"))}));
	Add(Station(TEXT("base_cosmetics"), TEXT("BASE LAUNCH PROFILE"),
				TEXT("A launch profile owns sound and an array of socket-bound Niagara effects."),
				TEXT("The base sound, muzzle flash, smoke, and casing effects play."),
				ENPSExampleScenario::Cosmetics,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity")));
	Add(Station(TEXT("modified_cosmetics"), TEXT("MODIFIED LAUNCH PROFILE"),
				TEXT("A contributor modifier changes the resolved launch presentation."),
				TEXT("The modifier changes effect scale, volume, pitch, and audible radius."),
				ENPSExampleScenario::Cosmetics,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity")));
	Add(Station(TEXT("sustained"), TEXT("SUSTAINED SEQUENCE"),
				TEXT("One sequence request produces cadence-controlled launch cosmetics without "
					 "individual timers."),
				TEXT("Launch presentation repeats at the configured sequence rate."),
				ENPSExampleScenario::Cosmetics,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity"),
				{Fact(TEXT("Sequence rate"), TEXT("12 per second"))}));
	Add(Station(
		TEXT("distance_cosmetics"), TEXT("COSMETICS BEYOND 100 CM NET CULL"),
		TEXT("Cosmetic routing is independent from actor relevancy distance."),
		TEXT("Remote listeners receive launch presentation while the launcher actor is culled."),
		ENPSExampleScenario::NetworkScale,
		TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_HighVelocity.DA_NPS_HighVelocity"),
		{Fact(TEXT("Net cull"), TEXT("100 cm")), Fact(TEXT("Cosmetic radius"), TEXT("2000 m"))}));
	Add(Station(TEXT("pooled_spread"), TEXT("POOLED 32 PROJECTILE SPREAD"),
				TEXT("A 32-projectile spread exercises pooling, tracer intervals, and aggregate "
					 "simulation."),
				TEXT("Each cycle launches 32 projectiles while the pool recovers retired actors."),
				ENPSExampleScenario::Performance,
				TEXT("/NPS/Examples/Data/Projectiles/DA_NPS_Stress.DA_NPS_Stress"),
				{Fact(TEXT("Projectiles per cycle"), TEXT("32")),
				 Fact(TEXT("Tracer interval"), TEXT("8"))}));

	for (FNPSExampleStationEntry& Entry : OutStations)
	{
		if (UBallisticsProjectileDataAsset* Projectile = Entry.ProjectileData.LoadSynchronous())
		{
			Entry.SpreadData = Projectile->SpreadConfig.Get();
			Entry.ExplosiveData = Projectile->ExplosiveConfig.Get();
		}
	}
	const TSoftObjectPtr<UBallisticsLaunchCosmeticProfile> Profile(FSoftObjectPath(
		TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchProfile.DA_NPS_LaunchProfile")));
	const TSoftObjectPtr<UBallisticsLaunchCosmeticModifier> Modifier(FSoftObjectPath(
		TEXT("/NPS/Examples/Data/Cosmetics/DA_NPS_LaunchModifier.DA_NPS_LaunchModifier")));
	for (FNPSExampleStationEntry& Entry : OutStations)
	{
		if (Entry.Scenario == ENPSExampleScenario::Cosmetics ||
			Entry.Scenario == ENPSExampleScenario::NetworkScale)
		{
			Entry.LaunchProfile = Profile;
		}
		if (Entry.ExampleId == TEXT("modified_cosmetics"))
		{
			Entry.LaunchModifier = Modifier;
		}
	}
}
