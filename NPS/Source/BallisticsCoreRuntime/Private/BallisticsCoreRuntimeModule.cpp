// NPS - Networked Projectile System
// Copyright Persidera Industries LLC. All Rights Reserved.

#include "BallisticsCoreRuntimeModule.h"
#include "BallisticsDebugDraw.h"

#define LOCTEXT_NAMESPACE "FBallisticsCoreRuntimeModule"

void FBallisticsCoreRuntimeModule::StartupModule()
{
	FBallisticsDebugDraw::RegisterConsoleCommands();
}

void FBallisticsCoreRuntimeModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBallisticsCoreRuntimeModule, BallisticsCoreRuntime)
