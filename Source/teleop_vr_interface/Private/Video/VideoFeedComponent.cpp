#include "Video/VideoFeedComponent.h"
#include "Teleop/OperatorPawn.h"
#include "Async/Async.h"
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

    // Every registered source starts decoding now and keeps running
    // continuously for the component's lifetime -- not just the active one.
    // This is what makes SetActiveSource a cheap pointer swap instead of a
    // pipeline teardown/rebuild (see SetActiveSource below).
    for (auto& Pair : Sources_)
    {
        Pair.Value->Initialize();
        Pair.Value->Start();
    }

    if (ActiveSource_)
        UE_LOG(LogTemp, Log, TEXT("VideoFeed: active source '%s'"), *ActiveSourceName_);

    UE_LOG(LogTemp, Log, TEXT("VideoFeed: BeginPlay complete, %d source(s), stereo=%s"),
        Sources_.Num(), bStereo_ ? TEXT("true") : TEXT("false"));
}

void UVideoFeedComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    // Restart tasks hold raw IVideoSource pointers into Sources_, so they must
    // all have finished before we Stop() or destroy anything.
    for (auto& Pair : Reconnect_)
    {
        if (Pair.Value.Pending.IsValid())
            Pair.Value.Pending.Wait();
    }
    Reconnect_.Empty();

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

    // ---- Per-source auto-reconnect -----------------------------------------
    // Runs for every registered source, not just the active one (stats reads
    // are cheap -- no texture copy involved), so a source currently off-screen
    // still gets recovered if it drops instead of only starting its timer once
    // someone switches to it.
    //
    // The restart is dispatched to a worker thread and this tick returns
    // immediately: Start() blocks inside GStreamer's state change (seconds, in
    // the worst case), and running that inline here stalled the game thread
    // hard enough to stop frame submission to the compositor.
    for (auto& Pair : Sources_)
    {
        IVideoSource* Src = Pair.Value.Get();
        FReconnectState& State = Reconnect_.FindOrAdd(Pair.Key);

        // A restart is still running -- leave this source completely alone.
        if (State.Pending.IsValid())
        {
            if (!State.Pending.IsReady())
                continue;
            State.Pending = TFuture<void>();   // finished; resume normal monitoring
            State.AccumSec = 0.0f;
        }

        if (Src->GetStats().bIsReceiving)
        {
            State.AccumSec = 0.0f;
            State.Attempt  = 0;      // healthy again -- collapse the backoff
            continue;
        }

        State.AccumSec += DeltaTime;
        const float Delay = ReconnectDelayFor(State.Attempt);
        if (State.AccumSec < Delay)
            continue;

        State.AccumSec = 0.0f;
        UE_LOG(LogTemp, Warning,
            TEXT("VideoFeed: no frames for %.0f s — restarting pipeline '%s' (attempt %d, next retry in %.0f s)"),
            Delay, *Pair.Key, State.Attempt + 1, ReconnectDelayFor(State.Attempt + 1));
        State.Attempt++;

        // Raw Src pointer is safe: EndPlay waits on every pending future before
        // Sources_ is touched, so the source outlives this task.
        State.Pending = Async(EAsyncExecution::ThreadPool, [Src]()
        {
            Src->Stop();
            Src->Initialize();
            Src->Start();
        });
    }

    if (!ActiveSource_ || !VideoTexture || IsSourceBusy(ActiveSourceName_)) return;

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

    // Auto-reconnect for the active source is handled by the generic
    // per-source loop at the top of this function (it covers every
    // registered source, active or not).
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

    // Deliberately no Stop()/Initialize()/Start() here: every registered
    // source is already running continuously since BeginPlay (see above), so
    // switching the active view is just repointing which one feeds the
    // texture -- no pipeline teardown, no reconnect hitch. Safe to call
    // often and repeatedly (e.g. from a future autonomous view-selection
    // policy).
    ActiveSourceName_ = Name;
    ActiveSource_ = Found->Get();

    UE_LOG(LogTemp, Log, TEXT("VideoFeed: active source -> '%s'"), *Name);
    return true;
}

FString UVideoFeedComponent::GetActiveSourceName() const { return ActiveSourceName_; }

// A restart task owns the source's internals while it runs (Initialize()
// destroys and rebuilds the receiver), so the game thread reports the source as
// simply "not receiving" for that window rather than racing it.
bool UVideoFeedComponent::IsSourceBusy(const FString& Name) const
{
    const FReconnectState* State = Reconnect_.Find(Name);
    return State && State->Pending.IsValid() && !State->Pending.IsReady();
}

float UVideoFeedComponent::ReconnectDelayFor(int32 Attempt) const
{
    const float Delay = kReconnectBaseSec * FMath::Pow(2.0f, static_cast<float>(FMath::Min(Attempt, 16)));
    return FMath::Min(Delay, kReconnectMaxSec);
}

FVideoSourceStats UVideoFeedComponent::GetStreamStats() const
{
    if (ActiveSource_ && !IsSourceBusy(ActiveSourceName_)) return ActiveSource_->GetStats();
    return FVideoSourceStats{};
}

bool UVideoFeedComponent::UpdateSourceTexture(const FString& Name, UTexture2D*& OutTexture)
{
    auto* Found = Sources_.Find(Name);
    if (!Found || IsSourceBusy(Name)) return false;
    return (*Found)->UpdateTexture(OutTexture);
}

FVideoSourceStats UVideoFeedComponent::GetSourceStats(const FString& Name) const
{
    auto* Found = Sources_.Find(Name);
    if (!Found || IsSourceBusy(Name)) return FVideoSourceStats{};
    return (*Found)->GetStats();
}

bool UVideoFeedComponent::IsReceiving() const
{
    if (ActiveSource_ && !IsSourceBusy(ActiveSourceName_)) return ActiveSource_->GetStats().bIsReceiving;
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

// HmdHFovDeg / PlaneDistance / FOVCoverage must match UGhostOverlayComponent's
// copies -- all three come from overlay.json so there is one source of truth.
// They also feed FGazeProjection (see OperatorPawn), so a mismatch mistargets
// gaze as well as misplacing the ghost.
void UVideoFeedComponent::UpdateLayerSize(int32 Width, int32 Height)
{
    if (!StereoLayer || Width <= 0 || Height <= 0) return;

    float HalfFOVRad = FMath::DegreesToRadians(HmdHFovDeg * 0.5f * FOVCoverage);
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