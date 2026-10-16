#include "Teleop/EpisodeEventReceiver.h"

#include <msgpack.hpp>
#include <map>
#include <string>

bool FEpisodeEventReceiver::Open(int32 ReceivePort) {
    if (ReceivePort <= 0) return false;
    UdpSocket::Config Cfg;
    Cfg.ReceivePort = ReceivePort;
    Socket_ = MakeUnique<UdpSocket>();
    Socket_->OnDataReceived.BindRaw(this, &FEpisodeEventReceiver::HandleDatagram);
    if (!Socket_->Open(Cfg)) {
        Socket_.Reset();
        UE_LOG(LogTemp, Warning, TEXT("EpisodeEventReceiver: could not bind port %d, log rotation off"), ReceivePort);
        return false;
    }
    UE_LOG(LogTemp, Log, TEXT("EpisodeEventReceiver: listening on %d"), ReceivePort);
    return true;
}

void FEpisodeEventReceiver::Close() {
    if (Socket_) { Socket_->Close(); Socket_.Reset(); }
}

void FEpisodeEventReceiver::HandleDatagram(const uint8* Data, int32 Size) {
    try {
        msgpack::object_handle oh = msgpack::unpack(reinterpret_cast<const char*>(Data), Size);
        std::map<std::string, msgpack::object> m;
        oh.get().convert(m);
        auto str = [&](const char* k) { auto it = m.find(k); return it == m.end() ? FString() : FString(UTF8_TO_TCHAR(it->second.as<std::string>().c_str())); };
        FEpisodeEvent Ev;
        Ev.Type      = str("type");
        Ev.SessionId = str("session_id");
        Ev.Reason    = str("reason");
        auto it = m.find("episode_index");
        if (it != m.end()) Ev.EpisodeIndex = it->second.as<int32_t>();
        Queue_.Enqueue(MoveTemp(Ev));
    } catch (...) {
        UE_LOG(LogTemp, Warning, TEXT("EpisodeEventReceiver: dropped undecodable datagram (%d bytes)"), Size);
    }
}
