// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "CQTest.h"
#include "Components/PIENetworkComponent.h"

#if ENABLE_PIE_NETWORK_TEST

#include "BallisticsProjectileDataAsset.h"
#include "Components/BoxComponent.h"
#include "Engine/NetConnection.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NPSExampleEnvironment.h"
#include "NPSFirstPersonExample.h"
#include "NPSNetworkBackend.h"

namespace
{
	void DisablePlaygroundTestMovement(ANPSPlaygroundCharacter* Character)
	{
		if (!Character)
		{
			return;
		}
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
		Movement->Deactivate();
		Movement->SetComponentTickEnabled(false);
	}
}

NETWORK_TEST_CLASS(FNPSPlaygroundNetworkTests, "NPS.Network.Playground")
{
	struct FNPSPlaygroundNetworkState : public FBasePIENetworkComponentState
	{
		ANPSPlaygroundCharacter* Character = nullptr;
		ANPSExampleTarget* Target = nullptr;
		double SettleUntil = 0.0;
	};

	FPIENetworkComponent<FNPSPlaygroundNetworkState> Network{
		TestRunner,
		TestCommandBuilder,
		bInitializing
	};

	BEFORE_EACH()
	{
		TestRunner->AddExpectedMessagePlain(
			TEXT("FNetGUIDCache::SupportsObject: Level /Temp/UEDPIE_"),
			ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains,
			-1);
		TestRunner->AddExpectedMessagePlain(
			TEXT("RegisterNetGUID_Client: Guid with pathname"),
			ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains,
			-1);

		FNetworkComponentBuilder<FNPSPlaygroundNetworkState>()
			.WithClients(1)
			.WithGameInstanceClass(UGameInstance::StaticClass())
			.WithGameMode(AGameModeBase::StaticClass())
			.Build(Network);
		Network.ThenServer(TEXT("Verify server replication backend"),
			[this](FNPSPlaygroundNetworkState& State)
			{
				ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
			})
			.ThenClients(TEXT("Verify client replication backend"),
				[this](FNPSPlaygroundNetworkState& State)
				{
					ASSERT_THAT(IsTrue(ValidateNPSNetworkBackend(*TestRunner, State.World)));
				});
	}

	void PreparePlayableLane()
	{
		Network
			.SpawnAndReplicate<
				ANPSExampleTarget,
				&FNPSPlaygroundNetworkState::Target>(
				[](ANPSExampleTarget& Target)
				{
					Target.SetReplicateMovement(true);
					Target.Collision->SetBoxExtent(FVector(100.f, 600.f, 600.f));
					Target.SetActorLocation(FVector(1000.f, 0.f, 164.f));
				})
			.SpawnAndReplicate<
				ANPSPlaygroundCharacter,
				&FNPSPlaygroundNetworkState::Character>(
				[](ANPSPlaygroundCharacter& Character)
				{
					Character.SetActorLocation(FVector(0.f, 0.f, 100.f));
					Character.SetActorRotation(FRotator::ZeroRotator);
				})
			.ThenServer(
				TEXT("Possess playground character"),
				[this](FNPSPlaygroundNetworkState& State)
				{
					ASSERT_THAT(AreEqual(State.ClientConnections.Num(), 1));
					APlayerController* ClientController =
						State.ClientConnections[0]->PlayerController;
					ASSERT_THAT(IsNotNull(ClientController));
					ASSERT_THAT(IsNotNull(State.Character));
					ClientController->Possess(State.Character);
					ClientController->SetControlRotation(FRotator::ZeroRotator);
					DisablePlaygroundTestMovement(State.Character);
					State.Character->SetActorLocation(FVector(0.f, 0.f, 100.f));
					State.Character->ForceNetUpdate();
				})
			.UntilClients(
				TEXT("Disable test character movement prediction"),
				[](FNPSPlaygroundNetworkState& State)
				{
					if (!State.Character || !State.Character->IsLocallyControlled())
					{
						return false;
					}
					DisablePlaygroundTestMovement(State.Character);
					State.Character->SetActorLocation(FVector(0.f, 0.f, 100.f));
					return !State.Character->GetCharacterMovement()
						->IsComponentTickEnabled();
				},
				FTimespan::FromSeconds(5))
			.UntilServer(
				TEXT("Wait for server weapon children"),
				[](FNPSPlaygroundNetworkState& State)
				{
					ANPSPlaygroundGun* Gun = State.Character
						? State.Character->GetCurrentGun()
						: nullptr;
					return Gun && Gun->GetOwner() == State.Character &&
						Gun->GetActiveProjectileData();
				},
				FTimespan::FromSeconds(5))
			.ThenClient(0, [](FNPSPlaygroundNetworkState& State)
				{
					State.Target->Collision->SetBoxExtent(FVector(100.f, 600.f, 600.f));
				})
			.UntilClients(
				TEXT("Wait for replicated playable character"),
				[](FNPSPlaygroundNetworkState& State)
				{
					ANPSPlaygroundGun* Gun = State.Character
						? State.Character->GetCurrentGun()
						: nullptr;
					return State.Character &&
						State.Character->IsLocallyControlled() &&
						State.Character->GetActorLocation().Equals(
							FVector(0.f, 0.f, 100.f), 2.f) &&
						State.Target &&
						State.Target->GetActorLocation().Equals(
							FVector(1000.f, 0.f, 164.f), 1.f) &&
						Gun && Gun->GetOwner() == State.Character &&
						Gun->HasLocalNetOwner() &&
						Gun->GetActiveProjectileData();
				},
				FTimespan::FromSeconds(5));
	}

	void ConfigureAndFire(
		int32 GunIndex,
		ELaunchAuthorityMode Mode,
		bool bExplosive,
		bool bWind)
	{
		PreparePlayableLane();
		Network
			.ThenClient(
				0,
				[GunIndex, Mode, bExplosive, bWind](
					FNPSPlaygroundNetworkState& State)
				{
					DisablePlaygroundTestMovement(State.Character);
					State.Character->SetPlaygroundConfiguration(
						GunIndex,
						Mode,
						bExplosive,
						bWind);
				})
			.UntilServer(
				TEXT("Wait for authoritative playground configuration"),
				[GunIndex, Mode, bExplosive, bWind](
					FNPSPlaygroundNetworkState& State)
				{
					ANPSPlaygroundGun* Gun =
						State.Character
							? State.Character->GetCurrentGun()
							: nullptr;
					UBallisticsProjectileDataAsset* Projectile =
						Gun ? Gun->GetActiveProjectileData() : nullptr;
					return State.Character &&
						Gun && Gun->GetOwner() == State.Character &&
						State.Character->GetSelectedGunIndex() == GunIndex &&
						State.Character->GetSelectedAuthorityMode() == Mode &&
						State.Character->IsExplosiveEnabled() == bExplosive &&
						State.Character->IsWindEnabled() == bWind &&
						Projectile &&
						Projectile->AuthorityModeOverride == Mode &&
						Projectile->IsExplosive() == bExplosive &&
						(!Projectile->WindAcceleration.IsNearlyZero()) == bWind;
				},
				FTimespan::FromSeconds(5))
			.UntilClients(
				TEXT("Wait for replicated playground configuration"),
				[GunIndex, Mode, bExplosive, bWind](
					FNPSPlaygroundNetworkState& State)
				{
					ANPSPlaygroundGun* Gun =
						State.Character
							? State.Character->GetCurrentGun()
							: nullptr;
					UBallisticsProjectileDataAsset* Projectile =
						Gun ? Gun->GetActiveProjectileData() : nullptr;
					return State.Character &&
						Gun && Gun->GetOwner() == State.Character &&
						Gun->HasLocalNetOwner() &&
						State.Character->GetSelectedGunIndex() == GunIndex &&
						State.Character->GetSelectedAuthorityMode() == Mode &&
						State.Character->IsExplosiveEnabled() == bExplosive &&
						State.Character->IsWindEnabled() == bWind &&
						Projectile &&
						Projectile->AuthorityModeOverride == Mode;
				},
				FTimespan::FromSeconds(5))
			.ThenClient(
				0,
				[this](FNPSPlaygroundNetworkState& State)
				{
					DisablePlaygroundTestMovement(State.Character);
					if (AController* Controller = State.Character->GetController())
					{
						Controller->SetControlRotation(FRotator::ZeroRotator);
					}
					ANPSPlaygroundGun* Gun = State.Character->GetCurrentGun();
					ASSERT_THAT(IsNotNull(Gun));
					ASSERT_THAT(IsTrue(Gun->FireOnce()));
				})
			.UntilServer(
				TEXT("Wait for playground target damage"),
				[](FNPSPlaygroundNetworkState& State)
				{
					return State.Target && State.Target->HitCount >= 1 &&
						State.Target->LastKineticEnergy > 0.f;
				},
				FTimespan::FromSeconds(20))
			.UntilClients(
				TEXT("Wait for replicated playground damage"),
				[](FNPSPlaygroundNetworkState& State)
				{
					return State.Target && State.Target->HitCount >= 1 &&
						State.Target->LastKineticEnergy > 0.f;
				},
				FTimespan::FromSeconds(20))
			.ThenServer(TEXT("Begin network drain"), [](FNPSPlaygroundNetworkState& State)
				{
					State.SettleUntil = State.World->GetTimeSeconds() + 0.5;
				})
			.UntilServer(TEXT("Drain reliable shot traffic"), [](FNPSPlaygroundNetworkState& State)
				{
					return State.World->GetTimeSeconds() >= State.SettleUntil;
				}, FTimespan::FromSeconds(2));
	}

	TEST_METHOD(ServerAuthoritative_RifleReplicatesConfigurationAndDamage)
	{
		ConfigureAndFire(
			0,
			ELaunchAuthorityMode::ServerAuthoritative,
			false,
			false);
	}

	TEST_METHOD(ClientPredicted_PistolReplicatesWindAndDamage)
	{
		ConfigureAndFire(
			1,
			ELaunchAuthorityMode::ClientPredicted,
			false,
			true);
	}

	TEST_METHOD(ClientAuthoritative_LauncherReplicatesExplosiveAndDamage)
	{
		ConfigureAndFire(
			2,
			ELaunchAuthorityMode::ClientAuthoritative,
			true,
			false);
	}
};

#endif
