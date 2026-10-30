// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsCoreEditorModule.h"
#include "BallisticsSpreadDataAsset.h"
#include "BallisticsSpreadDataAssetCustomization.h"
#include "PropertyEditorModule.h"

void FBallisticsCoreEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(
			TEXT("PropertyEditor"));
	PropertyEditor.RegisterCustomClassLayout(
		UBallisticsSpreadDataAsset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(
			&FBallisticsSpreadDataAssetCustomization::MakeInstance));
	PropertyEditor.NotifyCustomizationModuleChanged();
}

void FBallisticsCoreEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FPropertyEditorModule& PropertyEditor =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>(
				TEXT("PropertyEditor"));
		PropertyEditor.UnregisterCustomClassLayout(
			UBallisticsSpreadDataAsset::StaticClass()->GetFName());
		PropertyEditor.NotifyCustomizationModuleChanged();
	}
}

IMPLEMENT_MODULE(FBallisticsCoreEditorModule, BallisticsCoreEditor)
