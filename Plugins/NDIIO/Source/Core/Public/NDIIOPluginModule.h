/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>

#include <vector>
#include <algorithm>
#include <functional>
#include <chrono>

#include <Engine/World.h>
#include <Interfaces/IPluginManager.h>
#include <Modules/ModuleManager.h>
#include <IMediaPlayerFactory.h>

#include <NDIIOPluginSettings.h>

class NDIIO_API FNDIIOPluginModule
	: public IModuleInterface
	, public IMediaPlayerFactory
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** IMediaPlayerFactory implementation */
	virtual bool CanPlayUrl(const FString& Url, const IMediaOptions* /*Options*/, TArray<FText>* /*OutWarnings*/, TArray<FText>* OutErrors) const override;
	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) override;
	virtual FText GetDisplayName() const override;
	virtual FName GetPlayerName() const override;
	virtual FGuid GetPlayerPluginGUID() const override;
	virtual const TArray<FString>& GetSupportedPlatforms() const override;
	virtual bool SupportsFeature(EMediaFeature Feature) const override;


	bool BeginBroadcastingActiveViewport();
	void StopBroadcastingActiveViewport();

private:
	bool LoadModuleDependencies();
	void ShutdownModuleDependencies();

private:
	TSharedPtr<class FNDIFinderService> NDIFinderService = nullptr;
	TSharedPtr<class FNDIConnectionService> NDIConnectionService = nullptr;

	void* NDI_LIB_HANDLE = nullptr;

	/** List of platforms that the media player support. */
	TArray<FString> SupportedPlatforms;

	/** List of supported URI schemes. */
	TArray<FString> SupportedUriSchemes;
};
