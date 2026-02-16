/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#include "NDIShaders.h"

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

#include "Misc/Paths.h"
#include "Misc/EngineVersionComparison.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "PipelineStateCache.h"
#include "SceneUtils.h"
#include "SceneInterface.h"



BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNDIIOShaderUB, )
	SHADER_PARAMETER(uint32, InputWidth)
	SHADER_PARAMETER(uint32, InputHeight)
	SHADER_PARAMETER(uint32, OutputWidth)
	SHADER_PARAMETER(uint32, OutputHeight)
	SHADER_PARAMETER(FVector2f, UVOffset)
	SHADER_PARAMETER(FVector2f, UVScale)
	SHADER_PARAMETER(uint32, ColorCorrection)
	SHADER_PARAMETER(float, AlphaScale)
	SHADER_PARAMETER(float, AlphaOffset)
	SHADER_PARAMETER_TEXTURE(Texture2D, InputTarget)
	SHADER_PARAMETER_TEXTURE(Texture2D, InputAlphaTarget)
	SHADER_PARAMETER_SAMPLER(SamplerState, SamplerP)
	SHADER_PARAMETER_SAMPLER(SamplerState, SamplerB)
	SHADER_PARAMETER_SAMPLER(SamplerState, SamplerT)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNDIIOShaderUB, "NDIIOShaderUB");

IMPLEMENT_GLOBAL_SHADER(FNDIIOShaderVS, "/Plugin/NDIIOPlugin/Private/NDIIOShaders.usf", "NDIIOMainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FNDIIOShaderBGRAtoUYVYPS, "/Plugin/NDIIOPlugin/Private/NDIIOShaders.usf", "NDIIOBGRAtoUYVYPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FNDIIOShaderBGRAtoAlphaEvenPS, "/Plugin/NDIIOPlugin/Private/NDIIOShaders.usf", "NDIIOBGRAtoAlphaEvenPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FNDIIOShaderBGRAtoAlphaOddPS, "/Plugin/NDIIOPlugin/Private/NDIIOShaders.usf", "NDIIOBGRAtoAlphaOddPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FNDIIOShaderUYVYtoBGRAPS, "/Plugin/NDIIOPlugin/Private/NDIIOShaders.usf", "NDIIOUYVYtoBGRAPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FNDIIOShaderUYVAtoBGRAPS, "/Plugin/NDIIOPlugin/Private/NDIIOShaders.usf", "NDIIOUYVAtoBGRAPS", SF_Pixel);



void FNDIIOShaderPS::SetParameters(FRHICommandList& CommandList, const Params& params)
{
	FNDIIOShaderUB UB;
	{
		UB.InputWidth = params.InputTarget->GetSizeX();
		UB.InputHeight = params.InputTarget->GetSizeY();
		UB.OutputWidth = params.OutputSize.X;
		UB.OutputHeight = params.OutputSize.Y;
		UB.UVOffset = static_cast<FVector2f>(params.UVOffset);
		UB.UVScale = static_cast<FVector2f>(params.UVScale);
		UB.ColorCorrection = static_cast<uint32>(params.ColorCorrection);

		/*
		* Alpha' = Alpha * AlphaScale + AlphaOffset
		*        = (Alpha - AlphaMin) / (AlphaMax - AlphaMin)
		*        = Alpha / (AlphaMax - AlphaMin) - AlphaMin / (AlphaMax - AlphaMin)
		* AlphaScale = 1 / (AlphaMax - AlphaMin)
		* AlphaOffset = - AlphaMin / (AlphaMax - AlphaMin)
		*/
		float AlphaRange = params.AlphaMinMax[1] - params.AlphaMinMax[0];
		if (AlphaRange != 0.f)
		{
			UB.AlphaScale = 1.f / AlphaRange;
			UB.AlphaOffset = - params.AlphaMinMax[0] / AlphaRange;
		}
		else
		{
			UB.AlphaScale = 0.f;
			UB.AlphaOffset = -params.AlphaMinMax[0];
		}

		UB.InputTarget = params.InputTarget;
		UB.InputAlphaTarget = params.InputAlphaTarget;
		UB.SamplerP = TStaticSamplerState<SF_Point>::GetRHI();
		UB.SamplerB = TStaticSamplerState<SF_Bilinear>::GetRHI();
		UB.SamplerT = TStaticSamplerState<SF_Trilinear>::GetRHI();
	}

	TUniformBufferRef<FNDIIOShaderUB> Data = TUniformBufferRef<FNDIIOShaderUB>::CreateUniformBufferImmediate(UB, UniformBuffer_SingleFrame);
#if (ENGINE_MAJOR_VERSION > 5) || ((ENGINE_MAJOR_VERSION == 5) && (ENGINE_MINOR_VERSION >= 3))	// 5.3 or later
	FRHIBatchedShaderParameters& BatchedParameters = CommandList.GetScratchShaderParameters();
	SetUniformBufferParameter(BatchedParameters, GetUniformBufferParameter<FNDIIOShaderUB>(), Data);
	CommandList.SetBatchedShaderParameters(CommandList.GetBoundPixelShader(), BatchedParameters);
#else
	SetUniformBufferParameter(CommandList, CommandList.GetBoundPixelShader(), GetUniformBufferParameter<FNDIIOShaderUB>(), Data);
#endif
}


class FNDIIOShaders : public INDIIOShaders
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NDIIOPlugin"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/NDIIOPlugin"), PluginShaderDir);
   }
	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE( FNDIIOShaders, NDIIOShaders )
