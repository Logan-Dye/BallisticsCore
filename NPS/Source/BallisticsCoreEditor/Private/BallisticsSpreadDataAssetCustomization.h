// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class FBallisticsSpreadDataAssetCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
