// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "NPSExampleGuide.h"

#include "Components/InputComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

void ANPSExampleGuideHUD::EnsureMaps()
{
	if (!Maps.IsEmpty())
	{
		return;
	}

	if (const UNPSExampleCatalog* Catalog = UNPSExampleCatalog::LoadDefaultCatalog())
	{
		Maps = Catalog->Maps;
	}
	if (Maps.IsEmpty())
	{
		TArray<FNPSExampleStationEntry> Stations;
		UNPSExampleCatalog::BuildDefaultContent(Maps, Stations);
	}
	SelectedMapIndex = FMath::Clamp(SelectedMapIndex, 0, FMath::Max(Maps.Num() - 1, 0));
}

void ANPSExampleGuideHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	DrawGuideOverlay();
}

void ANPSExampleGuideHUD::DrawGuideOverlay()
{
	if (bGuideVisible)
	{
		const float Width = 370.f;
		const float X = FMath::Max(Canvas->ClipX - Width - 28.f, 12.f);
		const float Y = 28.f;
		DrawPanel(X, Y, Width, 178.f, FLinearColor(0.01f, 0.025f, 0.045f, 0.9f));
		DrawText(TEXT("NPS EXAMPLE GUIDE"), FLinearColor(0.1f, 0.8f, 1.f), X + 18.f, Y + 14.f,
				 GEngine->GetMediumFont(), 1.f, false);
		const FString Lines[] = {
			TEXT("H    SHOW OR HIDE THIS GUIDE"), TEXT("M    OPEN THE EXAMPLE MAP BROWSER"),
			TEXT("Station boards show purpose, expected result,"),
			TEXT("launch count, target hits, active projectiles, and pool size."),
			TEXT("Use the NPS.Debug commands for trajectory detail.")};
		float LineY = Y + 52.f;
		for (const FString& Line : Lines)
		{
			DrawText(Line, FLinearColor(0.78f, 0.84f, 0.9f), X + 18.f, LineY,
					 GEngine->GetSmallFont(), 1.f, false);
			LineY += 21.f;
		}
	}

	if (bMapBrowserVisible)
	{
		DrawMapBrowser();
	}
}

void ANPSExampleGuideHUD::DrawMapBrowser()
{
	EnsureMaps();
	const float Width = FMath::Min(780.f, Canvas->ClipX - 48.f);
	const float Height = FMath::Min(560.f, Canvas->ClipY - 48.f);
	const float X = (Canvas->ClipX - Width) * 0.5f;
	const float Y = (Canvas->ClipY - Height) * 0.5f;
	DrawPanel(X, Y, Width, Height, FLinearColor(0.005f, 0.015f, 0.03f, 0.96f));
	DrawText(TEXT("NPS EXAMPLE MAPS"), FLinearColor(0.1f, 0.8f, 1.f), X + 24.f, Y + 18.f,
			 GEngine->GetLargeFont(), 1.f, false);
	DrawText(TEXT("UP / DOWN SELECT    ENTER OPEN    M / ESC CLOSE"),
			 FLinearColor(0.65f, 0.72f, 0.8f), X + 26.f, Y + 58.f, GEngine->GetSmallFont(), 1.f,
			 false);

	float EntryY = Y + 96.f;
	for (int32 Index = 0; Index < Maps.Num(); ++Index)
	{
		const bool bSelected = Index == SelectedMapIndex;
		const FLinearColor Color =
			bSelected ? FLinearColor(1.f, 0.78f, 0.2f) : FLinearColor(0.75f, 0.82f, 0.88f);
		DrawText(FString::Printf(TEXT("%s %s"), bSelected ? TEXT(">") : TEXT(" "),
								 *Maps[Index].Title.ToString()),
				 Color, X + 28.f, EntryY, GEngine->GetSmallFont(), 1.f, false);
		EntryY += 28.f;
	}

	if (Maps.IsValidIndex(SelectedMapIndex))
	{
		const FNPSExampleMapEntry& Selected = Maps[SelectedMapIndex];
		DrawText(Selected.Summary.ToString(), FLinearColor(0.65f, 0.85f, 1.f), X + 330.f, Y + 105.f,
				 GEngine->GetSmallFont(), 1.f, false);
		DrawText(Selected.Map.ToSoftObjectPath().GetLongPackageName(),
				 FLinearColor(0.48f, 0.58f, 0.68f), X + 330.f, Y + 142.f, GEngine->GetSmallFont(),
				 0.9f, false);
	}
	if (!BrowserMessage.IsEmpty())
	{
		DrawText(BrowserMessage, FLinearColor(1.f, 0.55f, 0.2f), X + 26.f, Y + Height - 38.f,
				 GEngine->GetSmallFont(), 1.f, false);
	}
}

void ANPSExampleGuideHUD::DrawPanel(float X, float Y, float Width, float Height,
									const FLinearColor& Color)
{
	DrawRect(Color, X, Y, Width, Height);
	DrawLine(X, Y, X + Width, Y, FLinearColor(0.1f, 0.65f, 0.9f), 2.f);
}

void ANPSExampleGuideHUD::ToggleGuide()
{
	bGuideVisible = !bGuideVisible;
}

void ANPSExampleGuideHUD::ToggleMapBrowser()
{
	bMapBrowserVisible = !bMapBrowserVisible;
	BrowserMessage.Reset();
	EnsureMaps();
}

void ANPSExampleGuideHUD::SelectPreviousMap()
{
	if (!bMapBrowserVisible)
	{
		return;
	}
	EnsureMaps();
	SelectedMapIndex = Maps.IsEmpty() ? 0 : (SelectedMapIndex - 1 + Maps.Num()) % Maps.Num();
}

void ANPSExampleGuideHUD::SelectNextMap()
{
	if (!bMapBrowserVisible)
	{
		return;
	}
	EnsureMaps();
	SelectedMapIndex = Maps.IsEmpty() ? 0 : (SelectedMapIndex + 1) % Maps.Num();
}

void ANPSExampleGuideHUD::OpenSelectedMap()
{
	if (!bMapBrowserVisible)
	{
		return;
	}
	EnsureMaps();
	if (!Maps.IsValidIndex(SelectedMapIndex) || !GetWorld())
	{
		return;
	}

	APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController || GetWorld()->GetNetMode() == NM_Client)
	{
		BrowserMessage =
			TEXT("Only standalone players and the listen-server host can change maps.");
		return;
	}

	const FString MapPackage = Maps[SelectedMapIndex].Map.ToSoftObjectPath().GetLongPackageName();
	if (MapPackage.IsEmpty())
	{
		BrowserMessage = TEXT("The selected catalog entry has no map path.");
		return;
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer)
	{
		GetWorld()->ServerTravel(MapPackage);
	}
	else
	{
		UGameplayStatics::OpenLevel(this, FName(*MapPackage));
	}
}

void ANPSExampleGuideHUD::ClosePanels()
{
	bMapBrowserVisible = false;
	BrowserMessage.Reset();
}

void ANPSExamplePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (!PlayerInputComponent)
	{
		return;
	}
	PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &ANPSExamplePawn::ToggleGuide);
	PlayerInputComponent->BindKey(EKeys::M, IE_Pressed, this, &ANPSExamplePawn::ToggleMapBrowser);
	PlayerInputComponent->BindKey(EKeys::Up, IE_Pressed, this, &ANPSExamplePawn::SelectPreviousMap);
	PlayerInputComponent->BindKey(EKeys::Down, IE_Pressed, this, &ANPSExamplePawn::SelectNextMap);
	PlayerInputComponent->BindKey(EKeys::Enter, IE_Pressed, this,
								  &ANPSExamplePawn::OpenSelectedMap);
	PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ANPSExamplePawn::ClosePanels);
}

ANPSExampleGuideHUD* ANPSExamplePawn::GetGuideHUD() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	return PlayerController ? Cast<ANPSExampleGuideHUD>(PlayerController->GetHUD()) : nullptr;
}

void ANPSExamplePawn::ToggleGuide()
{
	if (ANPSExampleGuideHUD* HUD = GetGuideHUD())
		HUD->ToggleGuide();
}
void ANPSExamplePawn::ToggleMapBrowser()
{
	if (ANPSExampleGuideHUD* HUD = GetGuideHUD())
		HUD->ToggleMapBrowser();
}
void ANPSExamplePawn::SelectPreviousMap()
{
	if (ANPSExampleGuideHUD* HUD = GetGuideHUD())
		HUD->SelectPreviousMap();
}
void ANPSExamplePawn::SelectNextMap()
{
	if (ANPSExampleGuideHUD* HUD = GetGuideHUD())
		HUD->SelectNextMap();
}
void ANPSExamplePawn::OpenSelectedMap()
{
	if (ANPSExampleGuideHUD* HUD = GetGuideHUD())
		HUD->OpenSelectedMap();
}
void ANPSExamplePawn::ClosePanels()
{
	if (ANPSExampleGuideHUD* HUD = GetGuideHUD())
		HUD->ClosePanels();
}
