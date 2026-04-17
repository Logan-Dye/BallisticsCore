// BallisticsCore - Deterministic Projectile Ballistics
// Copyright Logan Dye. All Rights Reserved.

#include "Modules/ModuleManager.h"

class FBallisticsCoreTestsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FBallisticsCoreTestsModule, BallisticsCoreTests)
