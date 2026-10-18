#include "UI/NudgeReceiver.h"
#include "Input/TrackedControllerComponent.h"
#include "UI/SoundFeedback.h"
#include "Teleop/TeleOpLogger.h"
#include "Video/IntentHighlightComponent.h"
#include "Video/AlignmentPanelComponent.h"
#include "Shared/protocol.hpp"
#include "Misc/DateTime.h"

#include <msgpack.hpp>
#include <string>

UNudgeReceiver::UNudgeReceiver() {
    PrimaryComponentTick.bCanEverTick = true;
}

void UNudgeReceiver::Open(int32 ReceivePort, UTrackedControllerComponent* Left, UTrackedControllerComponent* Right,
                          USoundFeedback* Sound, FTeleOpLogger* Logger, UIntentHighlightComponent* Highlight) {
    Left_ = Left; Right_ = Right; Sound_ = Sound; Logger_ = Logger; Highlight_ = Highlight;
    if (ReceivePort <= 0) return;
    UdpSocket::Config Cfg;
    Cfg.ReceivePort = ReceivePort;
    Socket_ = MakeUnique<UdpSocket>();
    Socket_->OnDataReceived.BindUObject(this, &UNudgeReceiver::HandleDatagram);
    if (!Socket_->Open(Cfg)) {
        Socket_.Reset();
        UE_LOG(LogTemp, Warning, TEXT("NudgeReceiver: could not bind port %d, nudges disabled"), ReceivePort);
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("NudgeReceiver: listening on %d"), ReceivePort);
}

void UNudgeReceiver::EndPlay(const EEndPlayReason::Type Reason) {
    if (Socket_) { Socket_->Close(); Socket_.Reset(); }
    Super::EndPlay(Reason);
}

// Receive thread: decode only, rendering happens on the game thread in Tick.
void UNudgeReceiver::HandleDatagram(const uint8* Data, int32 Size) {
    try {
        msgpack::object_handle oh = msgpack::unpack(reinterpret_cast<const char*>(Data), Size);
        std::map<std::string, msgpack::object> m;
        oh.get().convert(m);
        auto str = [&](const char* k) { auto it = m.find(k); return it == m.end() ? FString() : FString(UTF8_TO_TCHAR(it->second.as<std::string>().c_str())); };
        auto num = [&](const char* k, double d) { auto it = m.find(k); return it == m.end() ? d : it->second.as<double>(); };
        const FString Type = str("type");
        if (Type == TEXT("highlight")) {
            if (!Highlight_) return;
            TArray<FHighlightTarget> Targets;
            auto it = m.find("targets");
            if (it != m.end()) {
                std::vector<std::map<std::string, msgpack::object>> raw;
                it->second.convert(raw);
                for (auto& t : raw) {
                    FHighlightTarget T;
                    auto n = t.find("name"); if (n != t.end()) T.Name = FString(UTF8_TO_TCHAR(n->second.as<std::string>().c_str()));
                    auto a = t.find("arm");  if (a != t.end()) T.Arm  = FString(UTF8_TO_TCHAR(a->second.as<std::string>().c_str()));
                    auto k = t.find("kind"); if (k != t.end()) T.bGoal = k->second.as<std::string>() == "goal";
                    auto p = t.find("prob"); if (p != t.end()) T.Prob = static_cast<float>(p->second.as<double>());
                    auto q = t.find("position");
                    if (q != t.end()) { std::vector<double> v; q->second.convert(v); for (int i = 0; i < 3 && i < (int)v.size(); ++i) T.Position[i] = static_cast<float>(v[i]); }
                    Targets.Add(T);
                }
            }
            Highlight_->SetTargets(Targets);
            return;
        }
        if (Type == TEXT("alignment")) {
            const FString Arm = str("arm");
            const int32 Idx = Arm == TEXT("left") ? 0 : (Arm == TEXT("right") ? 1 : -1);
            if (Idx < 0 || !Panels_[Idx]) return;
            auto flag = [&](const char* k) {
                auto it = m.find(k);
                if (it == m.end()) return false;
                if (it->second.type == msgpack::type::BOOLEAN) return it->second.as<bool>();
                return it->second.as<double>() != 0.0;
            };
            auto vec = [&](const char* k, float* Out, int N) {
                auto it = m.find(k);
                if (it == m.end()) return false;
                std::vector<double> v;
                it->second.convert(v);
                for (int i = 0; i < N && i < (int)v.size(); ++i) Out[i] = static_cast<float>(v[i]);
                return (int)v.size() >= N;
            };
            FAlignmentState S;
            S.bActive     = flag("active");
            S.Phase       = str("phase");
            vec("pos_err", S.PosErr, 3);
            S.YawErr      = static_cast<float>(num("yaw_err", 0.0));
            S.TargetYaw   = static_cast<float>(num("target_yaw", 0.0));
            vec("lean_err", S.Lean, 2);
            S.TolXY       = static_cast<float>(num("tol_xy", 0.005));
            S.TolYaw      = static_cast<float>(num("tol_yaw", 0.05));
            S.TolTilt     = static_cast<float>(num("tol_tilt", 0.026));
            S.ZRange      = static_cast<float>(num("z_range", 0.10));
            S.Depth       = static_cast<float>(num("depth", 0.0));
            S.bXYOk       = flag("xy_ok");
            S.bYawOk      = flag("yaw_ok");
            S.bTiltOk     = flag("tilt_ok");
            S.bTiltActive = flag("tilt_active");
            S.bDone       = flag("done");
            S.bTooFast    = flag("too_fast");
            S.bWedged     = flag("wedged");
            S.bHasAnchor  = vec("anchor", S.Anchor, 3);
            S.bHasCmd     = vec("cmd_err", S.CmdErr, 3);
            Panels_[Idx]->SetState(S);
            return;
        }
        if (Type != TEXT("cue_command")) return;
        FCueCommand Cue;
        Cue.Seq       = static_cast<uint32>(num("seq", 0));
        Cue.Nudge     = str("nudge");
        Cue.Channel   = str("channel");
        Cue.Primitive = str("primitive");
        Cue.Arm       = str("arm");
        Cue.Intensity = static_cast<float>(num("intensity", 0.0));
        Cue.DurationS = static_cast<float>(num("duration_s", 0.0));
        Cue.IssueNs   = static_cast<uint64>(num("t_issue_ns", 0.0));
        Queue_.Enqueue(MoveTemp(Cue));
    } catch (...) {
        UE_LOG(LogTemp, Warning, TEXT("NudgeReceiver: dropped undecodable datagram (%d bytes)"), Size);
    }
}

void UNudgeReceiver::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    FCueCommand Cue;
    while (Queue_.Dequeue(Cue)) {
        ++Received_;
        Render(Cue);
    }
}

void UNudgeReceiver::Render(const FCueCommand& Cue) {
    const float Duration = Cue.DurationS > 0.f ? Cue.DurationS : 0.03f;
    if (Cue.Channel == TEXT("haptic_left") || Cue.Channel == TEXT("haptic_right")) {
        UTrackedControllerComponent* T = Cue.Channel == TEXT("haptic_left") ? Left_ : Right_;
        if (T) T->PlayHaptic(FMath::Clamp(Cue.Intensity, 0.f, 1.f), Duration);
    } else if (Cue.Channel == TEXT("audio")) {
        if (Sound_) {
            const ESoundType Type = Cue.Primitive == TEXT("click") ? ESoundType::Contact : ESoundType::Warning;
            Sound_->Play(Type, FMath::Clamp(0.2f + 0.8f * Cue.Intensity, 0.f, 1.f));
        }
    }
    // visual primitives are not rendered yet; they are logged so the engine side can be checked

    if (Logger_) {
        const uint64 RenderNs = static_cast<uint64>(FDateTime::UtcNow().ToUnixTimestamp()) * 1000000000ULL
                              + static_cast<uint64>(FDateTime::UtcNow().GetMillisecond()) * 1000000ULL;
        Logger_->LogEvent(FString::Printf(TEXT("NUDGE seq=%u nudge=%s channel=%s primitive=%s arm=%s intensity=%.2f t_issue_ns=%llu t_render_ns=%llu"),
            Cue.Seq, *Cue.Nudge, *Cue.Channel, *Cue.Primitive, *Cue.Arm, Cue.Intensity, Cue.IssueNs, RenderNs));
    }
}
