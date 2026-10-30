// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/HUD.h"
#include "NPSExampleCatalog.h"
#include "NPSExampleGuide.generated.h"

UCLASS()
class NPSEXAMPLES_API ANPSExampleGuideHUD : public AHUD
{
	GENERATED_BODY()

  public:
	virtual void DrawHUD() override;

	void ToggleGuide();
	void ToggleMapBrowser();
	void SelectPreviousMap();
	void SelectNextMap();
	void OpenSelectedMap();
	void ClosePanels();

	bool IsGuideVisible() const
	{
		return bGuideVisible;
	}
	bool IsMapBrowserVisible() const
	{
		return bMapBrowserVisible;
	}
	int32 GetSelectedMapIndex() const
	{
		return SelectedMapIndex;
	}

  protected:
	void DrawGuideOverlay();

  private:
	TArray<FNPSExampleMapEntry> Maps;
	int32 SelectedMapIndex = 0;
	bool bGuideVisible = true;
	bool bMapBrowserVisible = false;
	FString BrowserMessage;

	void EnsureMaps();
	void DrawMapBrowser();
	void DrawPanel(float X, float Y, float Width, float Height, const FLinearColor& Color);
};

UCLASS()
class NPSEXAMPLES_API ANPSExamplePawn : public ADefaultPawn
{
	GENERATED_BODY()

  public:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

  private:
	void ToggleGuide();
	void ToggleMapBrowser();
	void SelectPreviousMap();
	void SelectNextMap();
	void OpenSelectedMap();
	void ClosePanels();
	ANPSExampleGuideHUD* GetGuideHUD() const;
};
