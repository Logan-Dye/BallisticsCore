// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "Widgets/SLeafWidget.h"

class UBallisticsSpreadDataAsset;

class SBallisticsSpreadPatternEditor : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SBallisticsSpreadPatternEditor)
		: _SpreadAsset(nullptr)
	{
	}
		SLATE_ARGUMENT(UBallisticsSpreadDataAsset*, SpreadAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override;

private:
	TWeakObjectPtr<UBallisticsSpreadDataAsset> SpreadAsset;
	TUniquePtr<class FScopedTransaction> ActiveTransaction;
	int32 DraggedPoint = INDEX_NONE;
	float ViewExtent = 5.f;

	float GetViewExtent() const;
	FVector2D PatternToLocal(
		const FVector2D& PatternPoint,
		const FVector2D& LocalSize) const;
	FVector2D LocalToPattern(
		const FVector2D& LocalPoint,
		const FVector2D& LocalSize) const;
	int32 FindPoint(
		const FVector2D& LocalPoint,
		const FVector2D& LocalSize) const;
	void NotifyPatternChanged(EPropertyChangeType::Type ChangeType) const;
};
