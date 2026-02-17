#include "VideoFeedComponent.h"
#include "OperatorPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Actor.h"

#undef UpdateResource

UVideoFeedComponent::UVideoFeedComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Tick after PoseMapper but before HUD
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UVideoFeedComponent::BeginPlay()
{
	Super::BeginPlay();

	// Get camera reference from owning pawn
	AOperatorPawn* Pawn = Cast<AOperatorPawn>(GetOwner());
	if (Pawn)
	{
		CameraRef = Pawn->GetVRCamera();
	}

	if (!CameraRef)
	{
		UE_LOG(LogTemp, Error, TEXT("VideoFeed: No VR camera found on owner pawn."));
		SetComponentTickEnabled(false);
		return;
	}

	CreateDisplayPlane();

	// Initialize and start all registered sources
	for (auto& Pair : Sources)
	{
		Pair.Value->Initialize();
	}

	// Start the active source if one was set
	if (ActiveSource)
	{
		ActiveSource->Start();
		UE_LOG(LogTemp, Log, TEXT("VideoFeed: Active source '%s' started."), *ActiveSourceName);
	}

	UE_LOG(LogTemp, Log, TEXT("VideoFeed: Initialized with %d source(s)."), Sources.Num());
}

void UVideoFeedComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Stop all sources
	for (auto& Pair : Sources)
	{
		Pair.Value->Stop();
	}

	Super::EndPlay(EndPlayReason);
}

void UVideoFeedComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!ActiveSource || !VideoTexture) return;

	// Check if source dimensions changed (auto-detect resolution)
	int32 SrcWidth, SrcHeight;
	if (ActiveSource->GetDimensions(SrcWidth, SrcHeight))
	{
		if (SrcWidth != CurrentTextureWidth || SrcHeight != CurrentTextureHeight)
		{
			EnsureTextureSize(SrcWidth, SrcHeight);
			UpdatePlaneScale(SrcWidth, SrcHeight);
		}
	}

	// Pull latest frame into texture
	ActiveSource->UpdateTexture(VideoTexture);
}

// ============================================================================
// Source Management
// ============================================================================

void UVideoFeedComponent::RegisterSource(const FString& Name, TUniquePtr<IVideoSource> Source)
{
	if (!Source)
	{
		UE_LOG(LogTemp, Warning, TEXT("VideoFeed: Attempted to register null source '%s'"), *Name);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("VideoFeed: Registered source '%s' (%s)"), *Name, *Source->GetSourceName());
	Sources.Add(Name, MoveTemp(Source));

	// If this is the first source, make it active by default
	if (Sources.Num() == 1)
	{
		ActiveSourceName = Name;
		ActiveSource = Sources[Name].Get();
	}
}

bool UVideoFeedComponent::SetActiveSource(const FString& Name)
{
	auto* Found = Sources.Find(Name);
	if (!Found)
	{
		UE_LOG(LogTemp, Warning, TEXT("VideoFeed: Source '%s' not found."), *Name);
		return false;
	}

	// Stop current source
	if (ActiveSource)
	{
		ActiveSource->Stop();
	}

	// Switch
	ActiveSourceName = Name;
	ActiveSource = Found->Get();

	// Initialize and start new source
	ActiveSource->Initialize();
	ActiveSource->Start();

	UE_LOG(LogTemp, Log, TEXT("VideoFeed: Switched to source '%s'"), *Name);
	return true;
}

FString UVideoFeedComponent::GetActiveSourceName() const
{
	return ActiveSourceName;
}

FVideoSourceStats UVideoFeedComponent::GetActiveStats() const
{
	if (ActiveSource)
	{
		return ActiveSource->GetStats();
	}
	return FVideoSourceStats();
}

bool UVideoFeedComponent::IsReceiving() const
{
	if (ActiveSource)
	{
		return ActiveSource->GetStats().bIsReceiving;
	}
	return false;
}

// ============================================================================
// Display Setup
// ============================================================================

void UVideoFeedComponent::CreateDisplayPlane()
{
	AActor* Owner = GetOwner();
	if (!Owner || !CameraRef) return;

	// Create the mesh component dynamically
	DisplayPlane = NewObject<UStaticMeshComponent>(Owner, TEXT("VideoDisplayPlane"));
	if (!DisplayPlane) return;

	// Load plane mesh
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane"));
	if (!PlaneMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("VideoFeed: Could not load plane mesh."));
		return;
	}

	DisplayPlane->SetStaticMesh(PlaneMesh);
	DisplayPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayPlane->SetCastShadow(false);

	// Attach face-locked to camera
	DisplayPlane->AttachToComponent(CameraRef, FAttachmentTransformRules::KeepRelativeTransform);

	// Position in front of camera, rotated to face the viewer
	DisplayPlane->SetRelativeLocation(FVector(PlaneDistance, 0.0f, 0.0f));
	//DisplayPlane->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	DisplayPlane->SetRelativeRotation(FRotator(0.0f, 90.0f, 90.0f));

	DisplayPlane->RegisterComponent();

	// Create default texture (will be resized on first frame)
	VideoTexture = UTexture2D::CreateTransient(1280, 720, PF_B8G8R8A8);
	if (VideoTexture)
	{
		VideoTexture->SRGB = true;
		VideoTexture->UpdateResource();
		CurrentTextureWidth = 1280;
		CurrentTextureHeight = 720;
	}

	// Create dynamic material
	UMaterial* BaseMat = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_VideoFeed.M_VideoFeed"));
	if (BaseMat)
	{
		DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMat, Owner);
		DynamicMaterial->SetTextureParameterValue(FName("VideoTexture"), VideoTexture);
		DisplayPlane->SetMaterial(0, DynamicMaterial);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VideoFeed: Material '/Game/Materials/M_VideoFeed' not found. Create an unlit material with a TextureSampleParameter2D named 'VideoTexture'."));
	}

	// Set initial scale for 16:9 at default distance
	UpdatePlaneScale(1280, 720);

	UE_LOG(LogTemp, Log, TEXT("VideoFeed: Display plane created at distance %.0f cm."), PlaneDistance);
}

void UVideoFeedComponent::UpdatePlaneScale(int32 Width, int32 Height)
{
	if (!DisplayPlane || Width <= 0 || Height <= 0) return;

	// Calculate plane size to cover FOVCoverage of the HMD FOV at PlaneDistance
	// Vive Pro Eye: ~110 degrees horizontal FOV
	float HalfFOVRad = FMath::DegreesToRadians(110.0f * 0.5f * FOVCoverage);
	float PlaneHalfWidth = PlaneDistance * FMath::Tan(HalfFOVRad);

	// Maintain aspect ratio
	float AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
	float PlaneHalfHeight = PlaneHalfWidth / AspectRatio;

	// Unreal's default plane is 100x100 units (1m x 1m), so scale accordingly
	float ScaleX = PlaneHalfWidth * 2.0f / 100.0f;
	float ScaleY = PlaneHalfHeight * 2.0f / 100.0f;

	DisplayPlane->SetRelativeScale3D(FVector(ScaleX, ScaleY, 1.0f));
}

void UVideoFeedComponent::EnsureTextureSize(int32 Width, int32 Height)
{
	if (Width == CurrentTextureWidth && Height == CurrentTextureHeight) return;
	if (Width <= 0 || Height <= 0) return;

	UE_LOG(LogTemp, Log, TEXT("VideoFeed: Resizing texture %dx%d -> %dx%d"),
		CurrentTextureWidth, CurrentTextureHeight, Width, Height);

	// Create new texture at the right size
	UTexture2D* NewTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!NewTexture) return;

	NewTexture->SRGB = true;
	NewTexture->UpdateResource();

	// Update material reference
	if (DynamicMaterial)
	{
		DynamicMaterial->SetTextureParameterValue(FName("VideoTexture"), NewTexture);
	}

	VideoTexture = NewTexture;
	CurrentTextureWidth = Width;
	CurrentTextureHeight = Height;
}
