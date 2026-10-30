// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsSpreadDataAssetCustomization.h"

#include "BallisticsSpreadDataAsset.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "SBallisticsSpreadPatternEditor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BallisticsSpreadDataAssetCustomization"

TSharedRef<IDetailCustomization>
FBallisticsSpreadDataAssetCustomization::MakeInstance()
{
	return MakeShared<FBallisticsSpreadDataAssetCustomization>();
}

void FBallisticsSpreadDataAssetCustomization::CustomizeDetails(
	IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1)
	{
		return;
	}

	UBallisticsSpreadDataAsset* SpreadAsset =
		Cast<UBallisticsSpreadDataAsset>(Objects[0].Get());
	if (!SpreadAsset)
	{
		return;
	}

	IDetailCategoryBuilder& Category =
		DetailBuilder.EditCategory(TEXT("Spread|Fixed Pattern"));
	Category.AddCustomRow(LOCTEXT("PatternEditorSearch", "Fixed Spread Pattern"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PatternEditorTitle", "Fixed Spread 2D Editor"))
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 4.f)
		[
			SNew(SBallisticsSpreadPatternEditor)
			.SpreadAsset(SpreadAsset)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 3.f, 0.f, 6.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT(
				"PatternEditorHelp",
				"Double-click to add. Drag to move. Right-click to remove. "
				"The points below and this editor use the same array."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.AutoWrapText(true)
		]
	];
}

#undef LOCTEXT_NAMESPACE
