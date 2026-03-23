#include "Video/VideoFeedComponent.h"
#include "Teleop/OperatorPawn.h"
#include "Camera/CameraComponent.h"
#include "Engine/Texture2D.h"
#include "RenderingThread.h"

UVideoFeedComponent::UVideoFeedComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UVideoFeedComponent::BeginPlay()
{
    Super::BeginPlay();

    AOperatorPawn* Pawn = Cast<AOperatorPawn>(GetOwner());
    if (Pawn)
        CameraRef = Pawn->GetVRCamera();

    if (!CameraRef)
    {
        UE_LOG(LogTemp, Error, TEXT("VideoFeed: no VR camera on owner pawn"));
        SetComponentTickEnabled(false);
        return;
    }

    CreateStereoLayer();

    for (auto& Pair : Sources_)
        Pair.Value->Initialize();

    if (ActiveSource_)
    {
        ActiveSource_->Start();
        UE_LOG(LogTemp, Log, TEXT("VideoFeed: started source '%s'"), *ActiveSourceName_);
    }

    UE_LOG(LogTemp, Log, TEXT("VideoFeed: BeginPlay complete, %d source(s)"), Sources_.Num());
}

void UVideoFeedComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    for (auto& Pair : Sources_)
        Pair.Value->Stop();

    FlushRenderingCommands();
    Super::EndPlay(EndPlayReason);
}

void UVideoFeedComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!ActiveSource_ || !VideoTexture) return;

    UTexture2D* PrevTexture = VideoTexture;
    UTexture2D* TexturePtr = VideoTexture;
    ActiveSource_->UpdateTexture(TexturePtr);

    if (TexturePtr != PrevTexture)
    {
        VideoTexture = TexturePtr;
        StereoLayer->SetTexture(VideoTexture);

        int32 W = 0, H = 0;
        if (ActiveSource_->GetDimensions(W, H))
            UpdateLayerSize(W, H);

        UE_LOG(LogTemp, Log, TEXT("VideoFeed: rebound stereo layer to new texture %dx%d"),
            VideoTexture->GetSizeX(), VideoTexture->GetSizeY());
    }
}

void UVideoFeedComponent::RegisterSource(const FString& Name, TUniquePtr<IVideoSource> Source)
{
    if (!Source)
    {
        UE_LOG(LogTemp, Warning, TEXT("VideoFeed: null source '%s' ignored"), *Name);
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("VideoFeed: registered source '%s' (%s)"), *Name, *Source->GetSourceName());
    Sources_.Add(Name, MoveTemp(Source));
    if (Sources_.Num() == 1)
    {
        ActiveSourceName_ = Name;
        ActiveSource_ = Sources_[Name].Get();
    }
}

bool UVideoFeedComponent::SetActiveSource(const FString& Name)
{
    auto* Found = Sources_.Find(Name);
    if (!Found)
    {
        UE_LOG(LogTemp, Warning, TEXT("VideoFeed: source '%s' not found"), *Name);
        return false;
    }

    if (ActiveSource_) ActiveSource_->Stop();

    ActiveSourceName_ = Name;
    ActiveSource_ = Found->Get();

    ActiveSource_->Initialize();
    ActiveSource_->Start();

    UE_LOG(LogTemp, Log, TEXT("VideoFeed: switched to source '%s'"), *Name);
    return true;
}

FString UVideoFeedComponent::GetActiveSourceName() const { return ActiveSourceName_; }

FVideoSourceStats UVideoFeedComponent::GetStreamStats() const
{
    if (ActiveSource_) return ActiveSource_->GetStats();
    return FVideoSourceStats{};
}

bool UVideoFeedComponent::IsReceiving() const
{
    if (ActiveSource_) return ActiveSource_->GetStats().bIsReceiving;
    return false;
}

void UVideoFeedComponent::CreateStereoLayer()
{
    AActor* Owner = GetOwner();
    if (!Owner || !CameraRef) return;

    VideoTexture = UTexture2D::CreateTransient(1280, 720, PF_B8G8R8A8);
    if (VideoTexture)
    {
        VideoTexture->SRGB = false;
        VideoTexture->Filter = TF_Bilinear;
        VideoTexture->LODGroup = TEXTUREGROUP_UI;
        VideoTexture->NeverStream = true;
        VideoTexture->UpdateResource();
    }

    StereoLayer = NewObject<UStereoLayerComponent>(Owner, TEXT("VideoStereoLayer"));
    StereoLayer->SetTexture(VideoTexture);
    StereoLayer->SetPriority(0);
    StereoLayer->bLiveTexture = true;
    StereoLayer->AttachToComponent(CameraRef, FAttachmentTransformRules::KeepRelativeTransform);
    StereoLayer->SetRelativeLocation(FVector(PlaneDistance, 0.0f, 0.0f));
    StereoLayer->RegisterComponent();

    UpdateLayerSize(1280, 720);
    UE_LOG(LogTemp, Log, TEXT("VideoFeed: stereo layer created at %.0f cm"), PlaneDistance);
}

void UVideoFeedComponent::UpdateLayerSize(int32 Width, int32 Height)
{
    if (!StereoLayer || Width <= 0 || Height <= 0) return;

    float HalfFOVRad = FMath::DegreesToRadians(110.0f * 0.5f * FOVCoverage);
    float HalfWidth = PlaneDistance * FMath::Tan(HalfFOVRad);
    float AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
    float HalfHeight = HalfWidth / AspectRatio;

    StereoLayer->SetQuadSize(FVector2D(HalfWidth * 2.0f, HalfHeight * 2.0f));
}