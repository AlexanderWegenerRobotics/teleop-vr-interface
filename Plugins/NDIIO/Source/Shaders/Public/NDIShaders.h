/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderResource.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "Misc/EngineVersionComparison.h"
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 2))	// 5.2 or later
#include "DataDrivenShaderPlatformInfo.h"
#endif

#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNDIIOShaders, Log, All);


class FNDIIOShaderVS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FNDIIOShaderVS, Global, NDIIOSHADERS_API);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	FNDIIOShaderVS()
	{}

	FNDIIOShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};


class FNDIIOShaderPS : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	FNDIIOShaderPS()
	{}

	FNDIIOShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}

	enum class EColorCorrection : uint32
	{
		None = 0,
		sRGBToLinear,
		LinearTosRGB
	};

	struct Params
	{
		Params(const TRefCountPtr<FRHITexture2D>& InputTargetIn, const TRefCountPtr<FRHITexture2D>& InputAlphaTargetIn, FIntPoint OutputSizeIn, FVector2D UVOffsetIn, FVector2D UVScaleIn, EColorCorrection ColorCorrectionIn, FVector2D AlphaMinMaxIn)
			: InputTarget(InputTargetIn)
			, InputAlphaTarget(InputAlphaTargetIn)
			, OutputSize(OutputSizeIn)
			, UVOffset(UVOffsetIn)
			, UVScale(UVScaleIn)
			, ColorCorrection(ColorCorrectionIn)
			, AlphaMinMax(AlphaMinMaxIn)
		{}

		TRefCountPtr<FRHITexture2D> InputTarget;
		TRefCountPtr<FRHITexture2D> InputAlphaTarget;
		FIntPoint OutputSize;
		FVector2D UVOffset;
		FVector2D UVScale;
		EColorCorrection ColorCorrection;
		FVector2D AlphaMinMax;
	};

	NDIIOSHADERS_API void SetParameters(FRHICommandList& CommandList, const Params& params);

protected:
};


class FNDIIOShaderBGRAtoUYVYPS : public FNDIIOShaderPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FNDIIOShaderBGRAtoUYVYPS, Global, NDIIOSHADERS_API);

public:
	using FNDIIOShaderPS::FNDIIOShaderPS;
};

class FNDIIOShaderBGRAtoAlphaEvenPS : public FNDIIOShaderPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FNDIIOShaderBGRAtoAlphaEvenPS, Global, NDIIOSHADERS_API);

public:
	using FNDIIOShaderPS::FNDIIOShaderPS;
};

class FNDIIOShaderBGRAtoAlphaOddPS : public FNDIIOShaderPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FNDIIOShaderBGRAtoAlphaOddPS, Global, NDIIOSHADERS_API);

public:
	using FNDIIOShaderPS::FNDIIOShaderPS;
};

class FNDIIOShaderUYVYtoBGRAPS : public FNDIIOShaderPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FNDIIOShaderUYVYtoBGRAPS, Global, NDIIOSHADERS_API);

public:
	using FNDIIOShaderPS::FNDIIOShaderPS;
};

class FNDIIOShaderUYVAtoBGRAPS : public FNDIIOShaderPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FNDIIOShaderUYVAtoBGRAPS, Global, NDIIOSHADERS_API);

public:
	using FNDIIOShaderPS::FNDIIOShaderPS;
};

class INDIIOShaders : public IModuleInterface
{
public:
};
