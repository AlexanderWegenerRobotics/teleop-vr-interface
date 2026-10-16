#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Networking/UdpSocket.h"

// One avatar episode boundary (teleop-simulator EpisodeEventMsg, msgpack map).
struct FEpisodeEvent {
    FString Type;          // episode_start | episode_end
    FString SessionId;
    int32   EpisodeIndex = -1;
    FString Reason;

    // Folder shared by every producer: <session_id>/<episode_index:03d>
    FString EpisodeDir() const { return FString::Printf(TEXT("%s/%03d"), *SessionId, EpisodeIndex); }
};

// UDP listener for the avatar's episode events; decoded on the socket thread, drained with Pop() on the game thread.
class TELEOP_VR_INTERFACE_API FEpisodeEventReceiver {
public:
    ~FEpisodeEventReceiver() { Close(); }

    bool Open(int32 ReceivePort);
    void Close();
    bool Pop(FEpisodeEvent& Out) { return Queue_.Dequeue(Out); }

private:
    void HandleDatagram(const uint8* Data, int32 Size);

    TUniquePtr<UdpSocket> Socket_;
    TQueue<FEpisodeEvent, EQueueMode::Mpsc> Queue_;
};
