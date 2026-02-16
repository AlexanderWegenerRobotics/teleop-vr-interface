/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <NDIIOPluginModule.h>

#include <Modules/ModuleManager.h>
#include <IMediaModule.h>
#include <NDIIOPluginAPI.h>
#include "Player/NDIMediaPlayer.h"
#include <Misc/Paths.h>

#include <GenericPlatform/GenericPlatformMisc.h>

#include <Services/NDIConnectionService.h>
#include <Services/NDIFinderService.h>

#include <Misc/MessageDialog.h>
#include <Misc/EngineVersionComparison.h>

// Meaning the plugin is being compiled with the editor
#if WITH_EDITOR

#include "ThumbnailRendering/ThumbnailManager.h"
#include "ThumbnailRendering/TextureThumbnailRenderer.h"

#include <ISettingsModule.h>
#include <Editor.h>

#include <Objects/Media/NDIMediaTexture2D.h>

#endif

#define LOCTEXT_NAMESPACE "FNDIIOPluginModule"


void FNDIIOPluginModule::StartupModule()
{
	// Doubly Ensure that this handle is nullptr
	NDI_LIB_HANDLE = nullptr;

	if (LoadModuleDependencies())
	{
#if UE_EDITOR

		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings(
				"Project", "Plugins", "NDI", LOCTEXT("NDISettingsName", "Vizrt NDI"),
				LOCTEXT("NDISettingsDescription", "Vizrt NDI(R) Engine Intergration Settings"),
				GetMutableDefault<UNDIIOPluginSettings>());
		}

		// Ensure that the thumbnail for the 'NDI Media Texture2D' is being updated, as the texture is being used.
		UThumbnailManager::Get().RegisterCustomRenderer(UNDIMediaTexture2D::StaticClass(),
														UTextureThumbnailRenderer::StaticClass());

#endif

		// Construct our Services
		this->NDIFinderService = MakeShareable(new FNDIFinderService());
		this->NDIConnectionService = MakeShareable(new FNDIConnectionService());

		// Start the service
		if (NDIFinderService.IsValid())
			NDIFinderService->Start();

		// Start the service
		if (NDIConnectionService.IsValid())
			NDIConnectionService->Start();
	}
	else
	{
#if PLATFORM_WINDOWS
		// Write an error message to the log.
		UE_LOG(LogWindows, Error,
			   TEXT("Unable to load \"Processing.NDI.Lib.x64.dll\" from the NDI 6 Runtime Directory."));

#if UE_EDITOR

		const FText& WarningMessage =
			LOCTEXT("NDIRuntimeMissing",
					"Cannot find \"Processing.NDI.Lib.x64.dll\" from the NDI 6 Runtime Directory. "
					"Continued usage of the plugin can cause instability within the editor.\r\n\r\n"

					"Please refer to the 'NDI IO Plugin for Unreal Engine Quickstart Guide' "
					"for additional information related to installation instructions for this plugin.\r\n\r\n");

		// Open a message box, showing that things will not work since the NDI Runtime Directory cannot be found
		if (FMessageDialog::Open(EAppMsgType::OkCancel, EAppReturnType::Ok, WarningMessage) == EAppReturnType::Ok)
		{
			FString URLResult = FString("");
			FPlatformProcess::LaunchURL(*FString("https://ndi.video/sdk/"), nullptr, &URLResult);
		}

#endif
#endif

#if (PLATFORM_LINUX || PLATFORM_LINUXARM64)
		// Write an error message to the log.
		UE_LOG(LogLinux, Error,
			   TEXT("Unable to load \"" NDILIB_LIBRARY_NAME "\" from the NDI 6 Runtime."));

#if UE_EDITOR

		const FText& WarningMessage =
			LOCTEXT("NDIRuntimeMissing",
					"Cannot find \"" NDILIB_LIBRARY_NAME "\" from the NDI 6 Runtime. "
					"Continued usage of the plugin can cause instability within the editor.\r\n\r\n"

					"Please refer to the 'NDI IO Plugin for Unreal Engine Quickstart Guide' "
					"for additional information related to installation instructions for this plugin.\r\n\r\n");

		// Open a message box, showing that things will not work since the NDI Runtime Directory cannot be found
		if (FMessageDialog::Open(EAppMsgType::OkCancel, EAppReturnType::Ok, WarningMessage) == EAppReturnType::Ok)
		{
			FString URLResult = FString("");
			FPlatformProcess::LaunchURL(*FString("https://ndi.video/sdk/"), nullptr, &URLResult);
		}

#endif
#endif
	}


	// supported platforms
	SupportedPlatforms.Add(TEXT("Windows"));
	SupportedPlatforms.Add(TEXT("Linux"));
	SupportedPlatforms.Add(TEXT("LinuxAArch64"));

	// supported schemes
	SupportedUriSchemes.Add(TEXT("ndiio"));

	// register player factory
	auto MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

	if (MediaModule != nullptr)
	{
		MediaModule->RegisterPlayerFactory(*this);
	}

	FApp::SetUnfocusedVolumeMultiplier(1.f);
}

void FNDIIOPluginModule::ShutdownModule()
{
	// unregister player factory
	auto MediaModule = FModuleManager::GetModulePtr<IMediaModule>("Media");

	if (MediaModule != nullptr)
	{
		MediaModule->UnregisterPlayerFactory(*this);
	}


	if (NDIFinderService.IsValid())
		NDIFinderService->Shutdown();

	ShutdownModuleDependencies();
}

bool FNDIIOPluginModule::BeginBroadcastingActiveViewport()
{
	// Ensure we have a valid service
	if (NDIConnectionService.IsValid())
	{
		// perform the requested functionality
		return NDIConnectionService->BeginBroadcastingActiveViewport();
	}

	return false;
}

void FNDIIOPluginModule::StopBroadcastingActiveViewport()
{
	// Ensure we have a valid service
	if (NDIConnectionService.IsValid())
	{
		// perform the requested functionality
		NDIConnectionService->StopBroadcastingActiveViewport();
	}
}




//~ IMediaPlayerFactory interface
bool FNDIIOPluginModule::CanPlayUrl(const FString& Url, const IMediaOptions* /*Options*/, TArray<FText>* /*OutWarnings*/, TArray<FText>* OutErrors) const
{
	FString Scheme;
	FString Location;

	// check scheme
	if (!Url.Split(TEXT("://"), &Scheme, &Location, ESearchCase::CaseSensitive))
	{
		if (OutErrors != nullptr)
		{
			OutErrors->Add(LOCTEXT("NoSchemeFound", "No URI scheme found"));
		}

		return false;
	}

	if (!SupportedUriSchemes.Contains(Scheme))
	{
		if (OutErrors != nullptr)
		{
			OutErrors->Add(FText::Format(LOCTEXT("SchemeNotSupported", "The URI scheme '{0}' is not supported"), FText::FromString(Scheme)));
		}

		return false;
	}

	return true;
}

TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> FNDIIOPluginModule::CreatePlayer(IMediaEventSink& EventSink)
{
	return MakeShared<FNDIMediaPlayer, ESPMode::ThreadSafe>(EventSink);
}

FText FNDIIOPluginModule::GetDisplayName() const
{
	return LOCTEXT("MediaPlayerDisplayName", "NDI Interface");
}

FName FNDIIOPluginModule::GetPlayerName() const
{
	static FName PlayerName(TEXT("NDIMedia"));
	return PlayerName;
}

FGuid FNDIIOPluginModule::GetPlayerPluginGUID() const
{
	static FGuid PlayerPluginGUID(0x71b13c2b, 0x70874965, 0x8a0e23f7, 0x5be6698f);
	return PlayerPluginGUID;
}

const TArray<FString>& FNDIIOPluginModule::GetSupportedPlatforms() const
{
	return SupportedPlatforms;
}

bool FNDIIOPluginModule::SupportsFeature(EMediaFeature Feature) const
{
	return Feature == EMediaFeature::AudioSamples ||
	       Feature == EMediaFeature::MetadataTracks ||
	       Feature == EMediaFeature::VideoSamples;
}




bool FNDIIOPluginModule::LoadModuleDependencies()
{
#if PLATFORM_WINDOWS
	// Get the Binaries File Location
	const FString env_variable = TEXT(NDILIB_REDIST_FOLDER);
	const FString binaries_path = FPlatformMisc::GetEnvironmentVariable(*env_variable) + "/Processing.NDI.Lib.x64.dll";

	// We can't validate if it's valid, but we can determine if it's explicitly not.
	if (binaries_path.Len() > 0)
	{
		// Load the DLL
		this->NDI_LIB_HANDLE = FPlatformProcess::GetDllHandle(*binaries_path);

		// Not required, but "correct" (see the SDK documentation)
		if (this->NDI_LIB_HANDLE != nullptr && !NDIlib_initialize())
		{
			// We were unable to initialize the library, so lets free the handle
			FPlatformProcess::FreeDllHandle(this->NDI_LIB_HANDLE);
			this->NDI_LIB_HANDLE = nullptr;
		}
	}

	// Did we successfully load the NDI library?
	return this->NDI_LIB_HANDLE != nullptr;
#endif

#if (PLATFORM_LINUX || PLATFORM_LINUXARM64)
	return true;
#endif
}

void FNDIIOPluginModule::ShutdownModuleDependencies()
{
#if PLATFORM_WINDOWS
	if (this->NDI_LIB_HANDLE != nullptr)
	{
		NDIlib_destroy();
		FPlatformProcess::FreeDllHandle(this->NDI_LIB_HANDLE);
		this->NDI_LIB_HANDLE = nullptr;
	}
#endif

#if (PLATFORM_LINUX || PLATFORM_LINUXARM64)
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNDIIOPluginModule, NDIIO);
