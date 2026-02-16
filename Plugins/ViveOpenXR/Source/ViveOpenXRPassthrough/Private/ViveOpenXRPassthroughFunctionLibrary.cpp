// Copyright HTC Corporation. All Rights Reserved.

#include "ViveOpenXRPassthroughFunctionLibrary.h"
#include "ViveOpenXRPassthrough.h"
#include "OpenXRHMD.h"

FViveOpenXRPassthrough* UViveOpenXRPassthroughFunctionLibrary::GetViveOpenXRPassthroughModulePtr()
{
	if (FViveOpenXRPassthroughPtr != nullptr)
	{
		return FViveOpenXRPassthroughPtr;
	}
	else
	{
		if (GEngine->XRSystem.IsValid())
		{
			auto HMD = static_cast<FOpenXRHMD*>(GEngine->XRSystem->GetHMDDevice());
			for (IOpenXRExtensionPlugin* Module : HMD->GetExtensionPlugins())
			{
				if (Module->GetDisplayName() == TEXT("ViveOpenXRPassthrough"))
				{
					FViveOpenXRPassthroughPtr = static_cast<FViveOpenXRPassthrough*>(Module);
					break;
				}
			}
		}
		return FViveOpenXRPassthroughPtr;
	}
}

float GetOpenXRHMDWorldToMeterScale()
{
	return static_cast<FOpenXRHMD*>(GEngine->XRSystem->GetHMDDevice())->GetWorldToMetersScale();
}

XrSpace GetOpenXRHMDTrackingSpace()
{
	return static_cast<FOpenXRHMD*>(GEngine->XRSystem->GetHMDDevice())->GetTrackingSpace();
}

bool UViveOpenXRPassthroughFunctionLibrary::IsPassthroughEnabled()
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;
	return GetViveOpenXRPassthroughModulePtr()->m_bEnablePassthrough;
}

FPassthroughHandle UViveOpenXRPassthroughFunctionLibrary::CreatePassthroughUnderlay(EXrPassthroughLayerForm inPassthroughLayerForm, bool AutoSwtich /*= true*/)
{
	PassthroughData Data;
	FPassthroughHandle Handle;
	if (!GetViveOpenXRPassthroughModulePtr()) return Handle;
	
	XrPassthroughFormHTC passthroughLayerForm = static_cast<XrPassthroughFormHTC>(inPassthroughLayerForm);
	Data = GetViveOpenXRPassthroughModulePtr()->CreatePassthrough(passthroughLayerForm);
	if (Data.Handle) {
		Handle.Handle = Data.Handle;
		Handle.Valid = Data.Valid;

		if (AutoSwtich)
		{
			UViveOpenXRPassthroughFunctionLibrary::SwitchPassthroughUnderlay(inPassthroughLayerForm);
		}
	}
	return Handle;
}

bool UViveOpenXRPassthroughFunctionLibrary::SwitchPassthroughUnderlay(EXrPassthroughLayerForm inPassthroughLayerForm)
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;

	XrPassthroughFormHTC passthroughLayerForm = static_cast<XrPassthroughFormHTC>(inPassthroughLayerForm);
	return GetViveOpenXRPassthroughModulePtr()->SwitchPassthrough(passthroughLayerForm);

}

bool UViveOpenXRPassthroughFunctionLibrary::DestroyPassthroughUnderlay(FPassthroughHandle PassthroughHandle)
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;

	return GetViveOpenXRPassthroughModulePtr()->DestroyPassthrough(PassthroughHandle.Handle);
}

bool UViveOpenXRPassthroughFunctionLibrary::SetPassthroughAlpha(FPassthroughHandle PassthroughHandle, float alpha)
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;

	return GetViveOpenXRPassthroughModulePtr()->SetPassthroughAlpha(PassthroughHandle.Handle, alpha);
}

XrVector3f* xrVertexBuffer = nullptr;
uint32_t* xrIndexBuffer = nullptr;

bool UViveOpenXRPassthroughFunctionLibrary::SetPassthroughMesh(FPassthroughHandle PassthroughHandle, const TArray<FVector>& vertices, const TArray<int32>& indices)
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;

	int numVertices = vertices.Num();
	int numIndices = indices.Num();
	if ((numIndices % 3) != 0)
		return false;

	if (xrVertexBuffer) delete xrVertexBuffer;
	if (xrIndexBuffer) delete xrIndexBuffer;

	xrVertexBuffer = new XrVector3f[numVertices];
	xrIndexBuffer = new uint32_t[numIndices];

	for (int i = 0; i < numVertices; i++)
	{
		xrVertexBuffer[i] = ToXrVector(vertices[i], GetOpenXRHMDWorldToMeterScale());
	}

	for (int i = 0; i < numIndices; i++)
	{
		xrIndexBuffer[i] = (uint32_t)indices[i];
	}

	return GetViveOpenXRPassthroughModulePtr()->SetPassthroughMesh(PassthroughHandle.Handle, (uint32_t)numVertices, xrVertexBuffer, (uint32_t)numIndices, xrIndexBuffer);
}

bool UViveOpenXRPassthroughFunctionLibrary::SetPassthroughMeshTransform(FPassthroughHandle PassthroughHandle, EProjectedPassthroughSpaceType meshSpaceType, FTransform meshTransform)
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;

	XrSpace meshSpace = (meshSpaceType == EProjectedPassthroughSpaceType::Headlock ? GetViveOpenXRPassthroughModulePtr()->GetHeadlockXrSpace() : GetViveOpenXRPassthroughModulePtr()->GetTrackingXrSpace());

	return GetViveOpenXRPassthroughModulePtr()->SetPassthroughMeshTransform(PassthroughHandle.Handle, meshSpace, ToXrPose(meshTransform, GetOpenXRHMDWorldToMeterScale()), XrVector3f{ (float)meshTransform.GetScale3D().Y, (float)meshTransform.GetScale3D().Z, (float)meshTransform.GetScale3D().X });
}

bool UViveOpenXRPassthroughFunctionLibrary::SetPassthroughMeshTransformSpace(FPassthroughHandle PassthroughHandle, EProjectedPassthroughSpaceType meshSpaceType)
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;

	XrSpace meshSpace = (meshSpaceType == EProjectedPassthroughSpaceType::Headlock ? GetViveOpenXRPassthroughModulePtr()->GetHeadlockXrSpace() : GetViveOpenXRPassthroughModulePtr()->GetTrackingXrSpace());

	return GetViveOpenXRPassthroughModulePtr()->SetPassthroughMeshTransformSpace(PassthroughHandle.Handle, meshSpace);
}

bool UViveOpenXRPassthroughFunctionLibrary::SetPassthroughMeshTransformLocation(FPassthroughHandle PassthroughHandle, FVector meshLcation)
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;

	return GetViveOpenXRPassthroughModulePtr()->SetPassthroughMeshTransformPosition(PassthroughHandle.Handle, ToXrVector(meshLcation, GetOpenXRHMDWorldToMeterScale()));
}

bool UViveOpenXRPassthroughFunctionLibrary::SetPassthroughMeshTransformRotation(FPassthroughHandle PassthroughHandle, FRotator meshRotation)
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;

	return GetViveOpenXRPassthroughModulePtr()->SetPassthroughMeshTransformOrientation(PassthroughHandle.Handle, ToXrQuat(meshRotation.Quaternion()));
}

bool UViveOpenXRPassthroughFunctionLibrary::SetPassthroughMeshTransformScale(FPassthroughHandle PassthroughHandle, FVector meshScale)
{
	if (!GetViveOpenXRPassthroughModulePtr()) return false;

	return GetViveOpenXRPassthroughModulePtr()->SetPassthroughMeshTransformScale(PassthroughHandle.Handle, XrVector3f{(float)meshScale.Y, (float)meshScale.Z, (float)meshScale.X});
}

ConfigurationRateType UViveOpenXRPassthroughFunctionLibrary::GetPassthroughImageRate()
{
	if (!GetViveOpenXRPassthroughModulePtr()) return ConfigurationRateType::Normal;

	return static_cast<ConfigurationRateType>(GetViveOpenXRPassthroughModulePtr()->GetPassthroughRate());
}

float UViveOpenXRPassthroughFunctionLibrary::GetPassthroughImageQuality()
{
	if (!GetViveOpenXRPassthroughModulePtr()) return 0;

	return GetViveOpenXRPassthroughModulePtr()->GetPassthroughQuality();
}