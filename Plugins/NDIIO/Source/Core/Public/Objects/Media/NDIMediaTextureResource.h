/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>
#include <ExternalTexture.h>
#include <TextureResource.h>
#include <Misc/EngineVersionComparison.h>

/**
	A Texture Resource object used by the NDIMediaTexture2D object for capturing video
	from a network source
*/
class NDIIO_API FNDIMediaTextureResource : public FTextureResource
{
public:
	/**
		Constructs a new instance of this object specifying a media texture owner

		@param Owner The media object used as the owner for this object
	*/
	FNDIMediaTextureResource(class UNDIMediaTexture2D* Owner = nullptr);

#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 3))	// 5.3 or later
	/** FTextureResource Interface Implementation for 'InitDynamicRHI' */
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	/** FTextureResource Interface Implementation for 'ReleaseDynamicRHI' */
	virtual void ReleaseRHI() override;
#else
	/** FTextureResource Interface Implementation for 'InitDynamicRHI' */
	virtual void InitDynamicRHI() override;

	/** FTextureResource Interface Implementation for 'ReleaseDynamicRHI' */
	virtual void ReleaseDynamicRHI() override;
#endif

	/** FTextureResource Interface Implementation for 'GetResourceSize' */
	SIZE_T GetResourceSize();

	/** FTextureResource Interface Implementation for 'GetSizeX' */
	virtual uint32 GetSizeX() const override;

	/** FTextureResource Interface Implementation for 'GetSizeY' */
	virtual uint32 GetSizeY() const override;

private:
	class UNDIMediaTexture2D* MediaTexture = nullptr;
};