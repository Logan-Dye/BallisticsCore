// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "SBallisticsSpreadPatternEditor.h"

#include "BallisticsSpreadDataAsset.h"
#include "Editor.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "SBallisticsSpreadPatternEditor"

namespace
{
	constexpr float EditorHeight = 320.f;
	constexpr float Padding = 24.f;
	constexpr float PointSize = 10.f;
	constexpr float HitRadius = 9.f;
}

void SBallisticsSpreadPatternEditor::Construct(const FArguments& InArgs)
{
	SpreadAsset = InArgs._SpreadAsset;
	SetCanTick(false);
}

FVector2D SBallisticsSpreadPatternEditor::ComputeDesiredSize(float) const
{
	return FVector2D(420.f, EditorHeight);
}

float SBallisticsSpreadPatternEditor::GetViewExtent() const
{
	float RequiredExtent = 0.f;
	if (const UBallisticsSpreadDataAsset* Asset = SpreadAsset.Get())
	{
		for (const FSpreadPatternPoint& Point : Asset->Pattern)
		{
			RequiredExtent = FMath::Max(
				RequiredExtent,
				FMath::Max(
					FMath::Abs(Point.YawOffset),
					FMath::Abs(Point.PitchOffset)));
		}
	}
	return FMath::Max(ViewExtent, RequiredExtent * 1.15f);
}

FVector2D SBallisticsSpreadPatternEditor::PatternToLocal(
	const FVector2D& PatternPoint,
	const FVector2D& LocalSize) const
{
	const float Extent = GetViewExtent();
	const FVector2D Center = LocalSize * 0.5f;
	const FVector2D DrawSize =
		(LocalSize - FVector2D(Padding * 2.f)).ComponentMax(FVector2D(1.f));
	return Center + FVector2D(
		PatternPoint.X / Extent * DrawSize.X * 0.5f,
		-PatternPoint.Y / Extent * DrawSize.Y * 0.5f);
}

FVector2D SBallisticsSpreadPatternEditor::LocalToPattern(
	const FVector2D& LocalPoint,
	const FVector2D& LocalSize) const
{
	const float Extent = GetViewExtent();
	const FVector2D Center = LocalSize * 0.5f;
	const FVector2D DrawSize =
		(LocalSize - FVector2D(Padding * 2.f)).ComponentMax(FVector2D(1.f));
	const FVector2D Offset = LocalPoint - Center;
	return FVector2D(
		FMath::Clamp(Offset.X / (DrawSize.X * 0.5f) * Extent, -Extent, Extent),
		FMath::Clamp(-Offset.Y / (DrawSize.Y * 0.5f) * Extent, -Extent, Extent));
}

int32 SBallisticsSpreadPatternEditor::FindPoint(
	const FVector2D& LocalPoint,
	const FVector2D& LocalSize) const
{
	const UBallisticsSpreadDataAsset* Asset = SpreadAsset.Get();
	if (!Asset)
	{
		return INDEX_NONE;
	}

	for (int32 Index = Asset->Pattern.Num() - 1; Index >= 0; --Index)
	{
		const FSpreadPatternPoint& Point = Asset->Pattern[Index];
		const FVector2D ScreenPoint =
			PatternToLocal(
				FVector2D(Point.YawOffset, Point.PitchOffset),
				LocalSize);
		if (FVector2D::Distance(ScreenPoint, LocalPoint) <= HitRadius)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void SBallisticsSpreadPatternEditor::NotifyPatternChanged(
	EPropertyChangeType::Type ChangeType) const
{
	if (UBallisticsSpreadDataAsset* Asset = SpreadAsset.Get())
	{
		FProperty* PatternProperty =
			FindFProperty<FProperty>(
				UBallisticsSpreadDataAsset::StaticClass(),
				GET_MEMBER_NAME_CHECKED(UBallisticsSpreadDataAsset, Pattern));
		FPropertyChangedEvent Event(PatternProperty, ChangeType);
		Asset->PostEditChangeProperty(Event);
		Asset->MarkPackageDirty();
	}
}

int32 SBallisticsSpreadPatternEditor::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2D Center = LocalSize * 0.5f;
	const FSlateBrush* WhiteBrush =
		FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FLinearColor Background(0.015f, 0.02f, 0.028f, 1.f);
	const FLinearColor GridColor(0.12f, 0.14f, 0.18f, 1.f);
	const FLinearColor AxisColor(0.38f, 0.42f, 0.5f, 1.f);
	const FLinearColor PointColor(0.05f, 0.8f, 1.f, 1.f);

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush,
		ESlateDrawEffect::None,
		Background);

	for (int32 GridIndex = -4; GridIndex <= 4; ++GridIndex)
	{
		const float Alpha = static_cast<float>(GridIndex + 4) / 8.f;
		const float X = FMath::Lerp(Padding, LocalSize.X - Padding, Alpha);
		const float Y = FMath::Lerp(Padding, LocalSize.Y - Padding, Alpha);
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(),
			{FVector2D(X, Padding), FVector2D(X, LocalSize.Y - Padding)},
			ESlateDrawEffect::None,
			GridIndex == 0 ? AxisColor : GridColor,
			true,
			GridIndex == 0 ? 1.5f : 1.f);
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(),
			{FVector2D(Padding, Y), FVector2D(LocalSize.X - Padding, Y)},
			ESlateDrawEffect::None,
			GridIndex == 0 ? AxisColor : GridColor,
			true,
			GridIndex == 0 ? 1.5f : 1.f);
	}

	const FSlateFontInfo Font =
		FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8);
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 2,
		AllottedGeometry.ToPaintGeometry(
			FVector2D(70.f, 14.f),
			FSlateLayoutTransform(FVector2D(LocalSize.X - 80.f, Center.Y + 4.f))),
		LOCTEXT("YawAxis", "Yaw right"),
		Font,
		ESlateDrawEffect::None,
		AxisColor);
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 2,
		AllottedGeometry.ToPaintGeometry(
			FVector2D(70.f, 14.f),
			FSlateLayoutTransform(FVector2D(Center.X + 5.f, 5.f))),
		LOCTEXT("PitchAxis", "Pitch up"),
		Font,
		ESlateDrawEffect::None,
		AxisColor);

	if (const UBallisticsSpreadDataAsset* Asset = SpreadAsset.Get())
	{
		for (int32 Index = 0; Index < Asset->Pattern.Num(); ++Index)
		{
			const FSpreadPatternPoint& Point = Asset->Pattern[Index];
			const FVector2D LocalPoint = PatternToLocal(
				FVector2D(Point.YawOffset, Point.PitchOffset),
				LocalSize);
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 3,
				AllottedGeometry.ToPaintGeometry(
					FVector2D(PointSize),
					FSlateLayoutTransform(
						LocalPoint - FVector2D(PointSize * 0.5f))),
				WhiteBrush,
				ESlateDrawEffect::None,
				Index == DraggedPoint
					? FLinearColor::White
					: PointColor);
			FSlateDrawElement::MakeText(
				OutDrawElements,
				LayerId + 4,
				AllottedGeometry.ToPaintGeometry(
					FVector2D(28.f, 12.f),
					FSlateLayoutTransform(LocalPoint + FVector2D(7.f, -10.f))),
				FText::AsNumber(Index),
				Font,
				ESlateDrawEffect::None,
				FLinearColor::White);
		}
	}

	const FString ExtentLabel =
		FString::Printf(TEXT("+/- %.2f deg"), GetViewExtent());
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId + 2,
		AllottedGeometry.ToPaintGeometry(
			FVector2D(90.f, 14.f),
			FSlateLayoutTransform(FVector2D(6.f, 5.f))),
		ExtentLabel,
		Font,
		ESlateDrawEffect::None,
		AxisColor);

	return LayerId + 5;
}

FReply SBallisticsSpreadPatternEditor::OnMouseButtonDown(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	UBallisticsSpreadDataAsset* Asset = SpreadAsset.Get();
	if (!Asset)
	{
		return FReply::Unhandled();
	}

	const FVector2D LocalPoint =
		MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const int32 PointIndex = FindPoint(LocalPoint, MyGeometry.GetLocalSize());

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton &&
		PointIndex != INDEX_NONE)
	{
		const FScopedTransaction Transaction(
			LOCTEXT("RemoveSpreadPoint", "Remove fixed spread point"));
		Asset->Modify();
		Asset->Pattern.RemoveAt(PointIndex);
		NotifyPatternChanged(EPropertyChangeType::ArrayRemove);
		return FReply::Handled();
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton &&
		PointIndex != INDEX_NONE)
	{
		ActiveTransaction = MakeUnique<FScopedTransaction>(
			LOCTEXT("MoveSpreadPoint", "Move fixed spread point"));
		Asset->Modify();
		DraggedPoint = PointIndex;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SBallisticsSpreadPatternEditor::OnMouseButtonDoubleClick(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	UBallisticsSpreadDataAsset* Asset = SpreadAsset.Get();
	if (!Asset ||
		MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	const FVector2D LocalPoint =
		MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D PatternPoint =
		LocalToPattern(LocalPoint, MyGeometry.GetLocalSize());
	const FScopedTransaction Transaction(
		LOCTEXT("AddSpreadPoint", "Add fixed spread point"));
	Asset->Modify();
	FSpreadPatternPoint& Point = Asset->Pattern.AddDefaulted_GetRef();
	Point.YawOffset = PatternPoint.X;
	Point.PitchOffset = PatternPoint.Y;
	NotifyPatternChanged(EPropertyChangeType::ArrayAdd);
	return FReply::Handled();
}

FReply SBallisticsSpreadPatternEditor::OnMouseButtonUp(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton ||
		DraggedPoint == INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	NotifyPatternChanged(EPropertyChangeType::ValueSet);
	DraggedPoint = INDEX_NONE;
	ActiveTransaction.Reset();
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SBallisticsSpreadPatternEditor::OnMouseMove(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	UBallisticsSpreadDataAsset* Asset = SpreadAsset.Get();
	if (!Asset || DraggedPoint == INDEX_NONE ||
		!Asset->Pattern.IsValidIndex(DraggedPoint))
	{
		return FReply::Unhandled();
	}

	const FVector2D LocalPoint =
		MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D PatternPoint =
		LocalToPattern(LocalPoint, MyGeometry.GetLocalSize());
	Asset->Pattern[DraggedPoint].YawOffset = PatternPoint.X;
	Asset->Pattern[DraggedPoint].PitchOffset = PatternPoint.Y;
	NotifyPatternChanged(EPropertyChangeType::Interactive);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

FReply SBallisticsSpreadPatternEditor::OnMouseWheel(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	const float ZoomFactor =
		FMath::Pow(0.85f, MouseEvent.GetWheelDelta());
	ViewExtent = FMath::Clamp(ViewExtent * ZoomFactor, 0.1f, 45.f);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
