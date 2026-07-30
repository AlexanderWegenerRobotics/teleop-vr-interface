#include "Video/LocalPreviewSource.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformTime.h"

bool FLocalPreviewSource::Initialize() {
    Socket_ = MakeUnique<UdpSocket>();
    Socket_->OnDataReceived.BindRaw(this, &FLocalPreviewSource::OnPacket);
    bUpdateInFlight_ = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
    return true;
}

bool FLocalPreviewSource::Start() {
    if (!Socket_) return false;
    UdpSocket::Config Cfg;
    Cfg.ReceivePort = Port;
    return Socket_->Open(Cfg);
}

void FLocalPreviewSource::Stop() {
    if (Socket_) Socket_->Close();
}

void FLocalPreviewSource::OnPacket(const uint8* Data, int32 Size) {
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Data, Size)) return;

    TArray64<uint8> Raw;
    if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)) return;

    FScopeLock Lock(&FrameLock_);
    PendingPixels_.SetNumUninitialized(Raw.Num());
    FMemory::Memcpy(PendingPixels_.GetData(), Raw.GetData(), Raw.Num());
    PendingWidth_  = Wrapper->GetWidth();
    PendingHeight_ = Wrapper->GetHeight();
    bHasNewFrame_  = true;
    LastFrameTime_ = FPlatformTime::Seconds();
}

bool FLocalPreviewSource::UpdateTexture(UTexture2D*& OutTexture) {
    TArray<uint8> Pixels;
    int32 Width = 0, Height = 0;
    {
        FScopeLock Lock(&FrameLock_);
        if (!bHasNewFrame_) return false;
        Pixels = MoveTemp(PendingPixels_);
        Width  = PendingWidth_;
        Height = PendingHeight_;
        bHasNewFrame_ = false;
    }

    if (!OutTexture || OutTexture->GetSizeX() != Width || OutTexture->GetSizeY() != Height) {
        UTexture2D* NewTex = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
        if (!NewTex) return false;
        NewTex->SRGB = false;
        NewTex->UpdateResource();
        OutTexture = NewTex;
    }

    VideoWidth_  = Width;
    VideoHeight_ = Height;

    if (!OutTexture->GetResource() || !OutTexture->GetResource()->TextureRHI)
        return false;

    if (bUpdateInFlight_->Load()) {
        if (FPlatformTime::Seconds() - UpdateInFlightStartTime_ > 0.1)
            bUpdateInFlight_->Store(false);
        else
            return false;
    }
    bUpdateInFlight_->Store(true);
    UpdateInFlightStartTime_ = FPlatformTime::Seconds();

    FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> PixelsPtr =
        MakeShared<TArray<uint8>, ESPMode::ThreadSafe>(MoveTemp(Pixels));
    TSharedPtr<TAtomic<bool>, ESPMode::ThreadSafe> InFlightFlag = bUpdateInFlight_;

    OutTexture->UpdateTextureRegions(
        0, 1, Region,
        static_cast<uint32>(Width * 4), 4,
        PixelsPtr->GetData(),
        [Region, PixelsPtr, InFlightFlag](uint8*, const FUpdateTextureRegion2D*)
        {
            delete Region;
            InFlightFlag->Store(false);
        });

    return true;
}

bool FLocalPreviewSource::GetDimensions(int32& OutWidth, int32& OutHeight) const {
    OutWidth  = VideoWidth_;
    OutHeight = VideoHeight_;
    return VideoWidth_ > 0 && VideoHeight_ > 0;
}

FVideoSourceStats FLocalPreviewSource::GetStats() const {
    FVideoSourceStats Out;
    FScopeLock Lock(&FrameLock_);
    Out.bIsReceiving = (FPlatformTime::Seconds() - LastFrameTime_) < 1.0;
    return Out;
}
