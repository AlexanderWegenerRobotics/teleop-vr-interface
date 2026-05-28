#include "Video/VideoFeedComponent.h"
#include "Teleop/OperatorPawn.h"
#include "Camera/CameraComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "RenderingThread.h"

#ifdef UpdateResource
#undef UpdateResource
#endif

UVideoFeedComponent::UVideoFeedComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> StereoMatFinder(
        TEXT("/Game/Materials/M_StereoVideoFeed.M_StereoVideoFeed"));
    if (StereoMatFinder.Succeeded())
        StereoVideoMaterial = StereoMatFinder.Object;
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

    UE_LOG(LogTemp, Log, TEXT("VideoFeed: BeginPlay complete, %d source(s), stereo=%s"),
        Sources_.Num(), bStereo_ ? TEXT("true") : TEXT("false"));
}

void UVideoFeedComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    for (auto& Pair : Sources_)
        Pair.Value->Stop();

    if (bStereo_ && PostProcessMID_ && CameraRef)
    {
        auto& Blendables = CameraRef->PostProcessSettings.WeightedBlendables.Array;
        Blendables.RemoveAll([this](const FWeightedBlendable& B) { return B.Object == PostProcessMID_; });
    }

    if (VideoTexture || StereoLayer)
        FlushRenderingCommands();

    Super::EndPlay(EndPlayReason);
}

void UVideoFeedComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!ActiveSource_ || !VideoTexture) return;

    UTexture2D* TexturePtr = VideoTexture;
    ActiveSource_->UpdateTexture(TexturePtr);

    if (TexturePtr != VideoTexture)
    {
        VideoTexture = TexturePtr;

        if (StereoLayer)
        {
            StereoLayer->SetTexture(VideoTexture);

            int32 W = 0, H = 0;
            if (ActiveSource_->GetDimensions(W, H))
                UpdateLayerSize(W, H);
        }

        UE_LOG(LogTemp, Log, TEXT("VideoFeed: rebound left/mono texture %dx%d"),
            VideoTexture->GetSizeX(), VideoTexture->GetSizeY());
    }

    if (bStereo_ && PostProcessMID_)
    {
        // Side-by-side mode: VideoTexture holds the combined 2560×960 frame
        // (left eye in the left half, right eye in the right half).
        // Both material params point to the same texture; the HLSL crops per eye.
        PostProcessMID_->SetTextureParameterValue(TEXT("VideoLeft"),  VideoTexture);
        PostProcessMID_->SetTextureParameterValue(TEXT("VideoRight"), VideoTexture);
    }
}

void UVideoFeedComponent::SetStereoMode(bool bStereo)
{
    // Side-by-side mode: a single combined stream carries both eyes.
    // No separate right-eye source needed.
    bStereo_ = bStereo;
    UE_LOG(LogTemp, Log, TEXT("VideoFeed: stereo mode %s (side-by-side combined stream)"),
        bStereo_ ? TEXT("enabled") : TEXT("disabled"));
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
    // Creates stereo layer(s) and initial textures; in stereo mode creates per-eye layers.
    AActor* Owner = GetOwner();
    if (!Owner || !CameraRef) return;

    auto MakeBlankTexture = [](const FName& DebugName) -> UTexture2D*
    {
        UTexture2D* Tex = UTexture2D::CreateTransient(1280, 960, PF_B8G8R8A8, DebugName);
        if (Tex)
        {
            Tex->SRGB        = false;
            Tex->Filter      = TF_Bilinear;
            Tex->LODGroup    = TEXTUREGROUP_UI;
            Tex->NeverStream = true;
            Tex->UpdateResource();
        }
        return Tex;
    };

    auto MakeLayer = [&](const FName& Name) -> UStereoLayerComponent*
    {
        UStereoLayerComponent* Layer = NewObject<UStereoLayerComponent>(Owner, Name);
        Layer->SetPriority(0);
        Layer->bLiveTexture = true;
        Layer->AttachToComponent(CameraRef, FAttachmentTransformRules::KeepRelativeTransform);
        Layer->SetRelativeLocation(FVector(PlaneDistance, 0.0f, 0.0f));
        return Layer;
    };

    // In side-by-side stereo mode the combined texture is 2×width.
    // Blank texture starts at 1×1; it will resize on first real frame.
    VideoTexture = MakeBlankTexture(TEXT("VideoTexCombined"));

    if (bStereo_) {
        if (StereoVideoMaterial)
        {
            PostProcessMID_ = UMaterialInstanceDynamic::Create(StereoVideoMaterial, this);
            // Both params point to the same combined texture; HLSL crops per eye.
            PostProcessMID_->SetTextureParameterValue(TEXT("VideoLeft"),  VideoTexture);
            PostProcessMID_->SetTextureParameterValue(TEXT("VideoRight"), VideoTexture);
            CameraRef->PostProcessSettings.WeightedBlendables.Array.Add(
                FWeightedBlendable(1.0f, PostProcessMID_));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("VideoFeed: stereo mode but StereoVideoMaterial not set — no video will render"));
        }

        UE_LOG(LogTemp, Log, TEXT("VideoFeed: post-process stereo material applied (side-by-side combined texture)"));
        return;
    }

    StereoLayer = MakeLayer(TEXT("VideoStereoLayer"));
    StereoLayer->SetTexture(VideoTexture);
    StereoLayer->RegisterComponent();
    UpdateLayerSize(1280, 960);
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

void UVideoFeedComponent::SetGhostTextures(UTextureRenderTarget2D* Left, UTextureRenderTarget2D* Right)
{
    if (!PostProcessMID_)
    {
        UE_LOG(LogTemp, Warning, TEXT("VideoFeed: SetGhostTextures called but PostProcessMID_ is null — ghost won't show"));
        return;
    }
    PostProcessMID_->SetTextureParameterValue(TEXT("GhostLeft"),  Left);
    PostProcessMID_->SetTextureParameterValue(TEXT("GhostRight"), Right);
    UE_LOG(LogTemp, Log, TEXT("VideoFeed: ghost eye RTs bound to video material (L=%s R=%s)"),
        Left  ? *Left->GetName()  : TEXT("null"),
        Right ? *Right->GetName() : TEXT("null"));
}