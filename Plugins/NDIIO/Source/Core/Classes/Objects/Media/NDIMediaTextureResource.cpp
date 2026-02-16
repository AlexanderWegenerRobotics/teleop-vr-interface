/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include <Objects/Media/NDIMediaTextureResource.h>

#include <RHI.h>
#include <DeviceProfiles/DeviceProfile.h>
#include <DeviceProfiles/DeviceProfileManager.h>
#include <Objects/Media/NDIMediaTexture2D.h>
#include <RenderUtils.h>

/**
	Constructs a new instance of this object specifying a media texture owner

	@param Owner The media object used as the owner for this object
*/
FNDIMediaTextureResource::FNDIMediaTextureResource(UNDIMediaTexture2D* Owner)
{
	this->MediaTexture = Owner;
}

#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 3))	// 5.3 or later
void FNDIMediaTextureResource::InitRHI(FRHICommandListBase& RHICmdList)
#else
void FNDIMediaTextureResource::InitDynamicRHI()
#endif
{
	if (this->MediaTexture != nullptr)
	{
		FSamplerStateInitializerRHI SamplerStateInitializer(
			(ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(
				MediaTexture),
			AM_Border, AM_Border, AM_Wrap);

		SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
	}
}

#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 3))	// 5.3 or later
void FNDIMediaTextureResource::ReleaseRHI()
#else
void FNDIMediaTextureResource::ReleaseDynamicRHI()
#endif
{
	// Release the TextureRHI bound by this object
	this->TextureRHI.SafeRelease();

	// Ensure that we have a owning media texture
	if (this->MediaTexture != nullptr)
	{
		// Remove the texture reference associated with the owner texture object
		RHIUpdateTextureReference(MediaTexture->TextureReference.TextureReferenceRHI, nullptr);
	}
}

SIZE_T FNDIMediaTextureResource::GetResourceSize()
{
	return CalcTextureSize(GetSizeX(), GetSizeY(), EPixelFormat::PF_A8R8G8B8, 1);
}

uint32 FNDIMediaTextureResource::GetSizeX() const
{
	return this->TextureRHI.IsValid() ? TextureRHI->GetSizeXYZ().X : 0;
}

uint32 FNDIMediaTextureResource::GetSizeY() const
{
	return this->TextureRHI.IsValid() ? TextureRHI->GetSizeXYZ().Y : 0;
}