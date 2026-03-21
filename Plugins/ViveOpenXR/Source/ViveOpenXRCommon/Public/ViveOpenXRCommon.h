// Copyright HTC Corporation. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


class FViveOpenXRCommon : public IModuleInterface
{
public:
	FViveOpenXRCommon();
	virtual ~FViveOpenXRCommon(){}

	// IModuleInterface
	virtual void StartupModule() override {  }
	virtual void ShutdownModule() override {  }
};
