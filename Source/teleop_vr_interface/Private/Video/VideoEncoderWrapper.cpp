#include "Video/VideoEncoderWrapper.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

struct FEncoderHandle
{
    HANDLE  hProcess = nullptr;
    HANDLE  hStdinWrite = nullptr;
    HANDLE  hStderrFile = nullptr;
    int32   FrameBytes = 0;
};

static FString FindFfmpeg()
{
    FString ProjectPath = FPaths::Combine(
        FPaths::ProjectDir(), TEXT("ThirdParty/ffmpeg/ffmpeg.exe"));
    if (FPaths::FileExists(ProjectPath)) return ProjectPath;
    return TEXT("ffmpeg");
}

// ---------------------------------------------------------------------------
// Encoder selection
// ---------------------------------------------------------------------------
// VideoLogger runs two of these concurrently (raw + attention), both 1280x720
// at 30 fps. In software that is two sustained real-time x264 encodes competing
// with the game thread for cores, which is the single largest CPU consumer in
// the process. On an NVIDIA card NVENC does the same work on dedicated silicon
// for effectively nothing.
//
// NVENC's option names changed between ffmpeg generations: older builds take
// -preset llhq / -rc vbr_hq, newer ones take -preset p1..p7 / -tune ll. The
// bundled ThirdParty/ffmpeg.exe is an older one. Rather than hardcode a guess,
// we probe candidates in order and keep the first that this binary actually
// accepts -- so upgrading ffmpeg later doesn't silently drop us back to
// software encoding.
//
// The probe runs the *exact* argument string we will use. Probing merely for
// "h264_nvenc exists" and then passing preset flags the build doesn't
// understand would fail at record time, where the symptom is an empty mp4.
// Hardware encoder candidates, tried in order; the first that survives a real
// end-to-end probe wins.
//
// NVENC is listed first even though it currently fails on this rig: the bundled
// ffmpeg is built against nvenc SDK 13.0, which needs NVIDIA driver >= 570, and
// the machine is on 566.14. That is a driver-version problem, not a capability
// one -- decode (NVDEC) has a lower floor, which is why nvh264dec works fine.
// Keeping NVENC at the top means that if the driver is ever updated, video logs
// silently move to it with no code change.
//
// h264_mf is the fallback that works today. It encodes through the Windows
// MediaFoundation H.264 MFT, which on an NVIDIA card is NVENC underneath but is
// reached via the OS rather than ffmpeg's bundled SDK headers -- so it is not
// gated on the SDK/driver pairing above. hw_encoding=true is important: without
// it MediaFoundation may quietly select its *software* MFT, which would defeat
// the entire point.
struct FEncoderCandidate { const TCHAR* Name; const TCHAR* Args; };

static const FEncoderCandidate kHwCandidates[] = {
    { TEXT("h264_nvenc"), TEXT("-c:v h264_nvenc -preset p4 -tune ll -rc vbr -cq 23 -b:v 0 -pix_fmt yuv420p") },
    { TEXT("h264_nvenc"), TEXT("-c:v h264_nvenc -preset llhq -rc vbr_hq -cq 23 -b:v 0 -pix_fmt yuv420p") },
    { TEXT("h264_nvenc"), TEXT("-c:v h264_nvenc -b:v 8M -pix_fmt yuv420p") },
    { TEXT("h264_mf"),    TEXT("-c:v h264_mf -hw_encoding true -b:v 8M -pix_fmt yuv420p") },
    { TEXT("h264_mf"),    TEXT("-c:v h264_mf -b:v 8M -pix_fmt yuv420p") },
};
// Software fallback. Two things here are deliberate and worth not "tidying":
//
//   -threads 4  x264 defaults to ~1.5x logical cores, which on a 24-thread part
//               means ~36 worker threads PER encoder. With two encoders that is
//               ~70 threads contending with the game and render threads, and the
//               damage shows up as frame-time jitter rather than as total CPU%.
//               Capping costs a little throughput we do not need at 720p30.
//
//   veryfast    ~2-3x cheaper than 'fast' at the same CRF. Quality target is
//               unchanged (still -crf 23); files come out somewhat larger.
static const TCHAR* kX264Args = TEXT("-c:v libx264 -preset veryfast -crf 23 -threads 4 -pix_fmt yuv420p");

static TAutoConsoleVariable<int32> CVarVideoLogEncoder(
    TEXT("teleop.VideoLog.Encoder"),
    0,
    TEXT("Encoder for VideoLogger's mp4 output. 0 = auto (probe hardware encoders, else x264), ")
    TEXT("1 = prefer hardware, 2 = force software x264."),
    ECVF_Default);

// Runs ffmpeg to completion, capturing its combined stdout/stderr. UE points
// both child handles at the same pipe, and we drain while it runs so a chatty
// failure can't fill the pipe buffer and deadlock the child.
static bool RunFfmpeg(const FString& Args, FString& OutOutput, int32& OutReturnCode)
{
    const FString Ffmpeg = FindFfmpeg();

    void* ReadPipe = nullptr;
    void* WritePipe = nullptr;
    if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
    {
        OutOutput = TEXT("CreatePipe failed");
        return false;
    }

    FProcHandle Proc = FPlatformProcess::CreateProc(
        *Ffmpeg, *Args, false, true, true, nullptr, 0, nullptr, WritePipe);

    if (!Proc.IsValid())
    {
        FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
        OutOutput = FString::Printf(TEXT("CreateProc failed for '%s'"), *Ffmpeg);
        return false;
    }

    while (FPlatformProcess::IsProcRunning(Proc))
    {
        OutOutput += FPlatformProcess::ReadPipe(ReadPipe);
        FPlatformProcess::Sleep(0.01f);
    }
    OutOutput += FPlatformProcess::ReadPipe(ReadPipe);

    OutReturnCode = -1;
    FPlatformProcess::GetProcReturnCode(Proc, &OutReturnCode);
    FPlatformProcess::CloseProc(Proc);
    FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
    return true;
}

// Runs a short real encode with the given argument string; exit code 0 means
// this ffmpeg build and this machine accept it end to end. On failure the
// reason is logged -- "Unknown encoder", "Cannot load nvEncodeAPI64.dll" and
// "No capable devices found" are very different problems and we cannot tell
// them apart from an exit code alone.
static bool TryEncoderArgs(const TCHAR* Args)
{
    // 256x256 rather than something tiny: NVENC rejects very small frames on some
    // driver/SDK combinations, and a false negative here would silently cost us
    // the hardware path for the whole session.
    //
    // -nostdin is load-bearing. UE's CreateProc sets STARTF_USESTDHANDLES so it can
    // redirect the child's stdout/stderr into our pipe, but it leaves hStdInput NULL.
    // ffmpeg reads stdin for interactive keyboard commands by default, and against an
    // invalid handle that fails immediately -- for EVERY probe, whatever the encoder.
    // Without this the probe reports "no NVENC variant accepted" on a machine where
    // NVENC is perfectly fine.
    const FString FullArgs = FString::Printf(
        TEXT("-hide_banner -nostdin -loglevel error -f lavfi -i color=c=black:s=256x256:d=0.1 -r 30 %s -f null -"),
        Args);

    FString Output;
    int32 ReturnCode = -1;
    const bool bLaunched = RunFfmpeg(FullArgs, Output, ReturnCode);

    if (!bLaunched || ReturnCode != 0)
    {
        Output.TrimStartAndEndInline();
        UE_LOG(LogTemp, Log,
            TEXT("VideoEncoderWrapper: probe rejected [%s] (exit=%d): %s"),
            Args, ReturnCode, Output.IsEmpty() ? TEXT("<no output>") : *Output.Left(400));
        return false;
    }
    return true;
}

// Returns the encoder argument string and its name, honouring the CVar override.
// Result is cached: it is a property of the machine and ffmpeg build, not of any
// one recording, and both VideoLogger encoders ask for it.
static const TCHAR* SelectEncoderArgs(const TCHAR*& OutName)
{
    const int32 Override = CVarVideoLogEncoder.GetValueOnAnyThread();
    if (Override == 2) { OutName = TEXT("libx264 (forced)"); return kX264Args; }

    // NOTE: this cache lives for the whole editor process, not the PIE session.
    // A Live Coding patch cannot reset an already-initialised static, so once the
    // probe has run, editing this file and hot-patching will NOT re-probe -- the
    // old verdict silently persists and no probe lines appear at all. Restart the
    // editor to force a fresh probe. The "(cached ...)" log below exists so that
    // situation is visible rather than looking like the code never compiled.
    static const TCHAR* Cached = nullptr;
    static const TCHAR* CachedName = nullptr;

    if (Cached)
    {
        UE_LOG(LogTemp, Log,
            TEXT("VideoEncoderWrapper: using %s (cached from the probe earlier in this editor session; ")
            TEXT("restart the editor to re-probe)"), CachedName);
    }

    if (!Cached)
    {
        for (const FEncoderCandidate& Candidate : kHwCandidates)
        {
            if (TryEncoderArgs(Candidate.Args))
            {
                Cached     = Candidate.Args;
                CachedName = Candidate.Name;
                UE_LOG(LogTemp, Log,
                    TEXT("VideoEncoderWrapper: hardware encode probe PASSED (%s) — video logs are off the CPU [%s]"),
                    Candidate.Name, Candidate.Args);
                break;
            }
        }

        if (!Cached)
        {
            Cached     = kX264Args;
            CachedName = TEXT("libx264");

            // Control probe: if even x264 fails here, the harness is at fault
            // (ffmpeg not found, lavfi/color filter missing, pipe/exit-code
            // plumbing wrong) and the NVENC rejections above say nothing about
            // NVENC. Worth one extra process launch to avoid chasing the wrong
            // problem -- this branch only runs once, and only when we already
            // decided to fall back.
            if (TryEncoderArgs(kX264Args))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("VideoEncoderWrapper: no hardware encoder accepted (x264 control probe PASSED, ")
                    TEXT("so the probe is sound). Falling back to software x264 — see the 'probe rejected' ")
                    TEXT("lines above for each encoder's reason."));
            }
            else
            {
                UE_LOG(LogTemp, Error,
                    TEXT("VideoEncoderWrapper: x264 control probe ALSO failed — the capability probe ")
                    TEXT("itself is broken (ffmpeg path, lavfi/color filter, or exit-code plumbing), ")
                    TEXT("not NVENC. See the 'probe rejected' lines above for ffmpeg's own message."));
            }
        }
    }

    // "Prefer hardware" still uses the probed variant when we found one, so the
    // override can never select an argument set this build rejects.
    if (Override == 1 && Cached == kX264Args)
    {
        OutName = TEXT("libx264 (hardware forced, but no hardware encoder passed the probe)");
        return kX264Args;
    }

    OutName = CachedName;
    return Cached;
}

FEncoderHandle* VideoEncoder_Create(int32 Width, int32 Height, int32 FPS,
    const FString& OutputPath)
{
    FString FfmpegPath = FindFfmpeg();

    // Create an inheritable pipe for ffmpeg's stdin.
    SECURITY_ATTRIBUTES SA;
    ZeroMemory(&SA, sizeof(SA));
    SA.nLength = sizeof(SA);
    SA.bInheritHandle = 1;
    SA.lpSecurityDescriptor = nullptr;

    HANDLE hStdinRead = nullptr;
    HANDLE hStdinWrite = nullptr;
    if (!CreatePipe(&hStdinRead, &hStdinWrite, &SA, 0))
    {
        UE_LOG(LogTemp, Error, TEXT("VideoEncoderWrapper: CreatePipe failed (%d)"),
            (int32)GetLastError());
        return nullptr;
    }

    // The write end must NOT be inherited by the child.
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    // Create an inheritable file handle for ffmpeg's stderr log.
    FString FfmpegLogFile = FPaths::ChangeExtension(OutputPath, TEXT(".ffmpeg.log"));
    HANDLE hStderrFile = CreateFileW(
        *FfmpegLogFile,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &SA,            // inheritable
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    // Build command line. Encoder is chosen once per process (see SelectEncoderArgs):
    // NVENC when this machine and ffmpeg build actually accept it, else x264.
    const TCHAR* EncoderName = nullptr;
    const TCHAR* EncoderArgs = SelectEncoderArgs(EncoderName);

    FString CmdLine = FString::Printf(
        TEXT("\"%s\" -y -loglevel info -f rawvideo -pix_fmt yuv420p -s %dx%d -r %d -i - "
            "%s -movflags +faststart \"%s\""),
        *FfmpegPath, Width, Height, FPS, EncoderArgs, *OutputPath);

    STARTUPINFOW StartInfo;
    ZeroMemory(&StartInfo, sizeof(StartInfo));
    StartInfo.cb = sizeof(StartInfo);
    StartInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    StartInfo.hStdInput = hStdinRead;
    StartInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    StartInfo.hStdError = (hStderrFile != INVALID_HANDLE_VALUE) ? hStderrFile : GetStdHandle(STD_ERROR_HANDLE);
    StartInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION ProcInfo;
    ZeroMemory(&ProcInfo, sizeof(ProcInfo));

    BOOL bOK = CreateProcessW(
        nullptr,
        const_cast<LPWSTR>(*CmdLine),
        nullptr, nullptr,
        1,              // bInheritHandles
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &StartInfo, &ProcInfo);

    // Close the read end of stdin pipe — ffmpeg owns it now.
    CloseHandle(hStdinRead);

    if (!bOK)
    {
        UE_LOG(LogTemp, Error,
            TEXT("VideoEncoderWrapper: CreateProcess failed (%d) for '%s'"),
            (int32)GetLastError(), *FfmpegPath);
        CloseHandle(hStdinWrite);
        if (hStderrFile != INVALID_HANDLE_VALUE) CloseHandle(hStderrFile);
        return nullptr;
    }

    // We don't need the thread handle.
    CloseHandle(ProcInfo.hThread);

    FEncoderHandle* H = new FEncoderHandle();
    H->hProcess = ProcInfo.hProcess;
    H->hStdinWrite = hStdinWrite;
    H->hStderrFile = hStderrFile;
    H->FrameBytes = Width * Height * 3 / 2;

    UE_LOG(LogTemp, Log,
        TEXT("VideoEncoderWrapper: ffmpeg started (PID=%d, %dx%d @%dfps, %s) -> %s"),
        (int32)ProcInfo.dwProcessId, Width, Height, FPS, EncoderName, *OutputPath);
    return H;
}

void VideoEncoder_SubmitYUV420P(FEncoderHandle* Handle,
    const uint8* Y, const uint8* U, const uint8* V,
    int32 Width, int32 Height)
{
    if (!Handle || !Handle->hStdinWrite) return;

    const int32 NumPx = Width * Height;
    const int32 UVSize = NumPx / 4;

    // Write Y, U, V planes sequentially. WriteFile blocks if the
    // pipe buffer is full, providing natural backpressure.
    auto WriteFull = [&](const uint8* Data, int32 Size) -> bool
        {
            DWORD TotalWritten = 0;
            while (TotalWritten < (DWORD)Size)
            {
                DWORD Written = 0;
                if (!WriteFile(Handle->hStdinWrite, Data + TotalWritten,
                    Size - TotalWritten, &Written, nullptr))
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("VideoEncoderWrapper: WriteFile failed (%d)"),
                        (int32)GetLastError());
                    return false;
                }
                TotalWritten += Written;
            }
            return true;
        };

    WriteFull(Y, NumPx);
    WriteFull(U, UVSize);
    WriteFull(V, UVSize);
}

void VideoEncoder_Destroy(FEncoderHandle* Handle)
{
    if (!Handle) return;

    // Close the write end of the pipe. ffmpeg sees EOF on stdin,
    // flushes remaining frames, writes the moov atom, and exits.
    if (Handle->hStdinWrite)
    {
        CloseHandle(Handle->hStdinWrite);
        Handle->hStdinWrite = nullptr;
    }

    // Wait for ffmpeg to finalize the MP4 container.
    if (Handle->hProcess)
    {
        DWORD WaitResult = WaitForSingleObject(Handle->hProcess, 15000);
        if (WaitResult == WAIT_TIMEOUT)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("VideoEncoderWrapper: ffmpeg did not exit in 15s, terminating"));
            TerminateProcess(Handle->hProcess, 1);
            WaitForSingleObject(Handle->hProcess, 3000);
        }

        DWORD ExitCode = 0;
        GetExitCodeProcess(Handle->hProcess, &ExitCode);
        UE_LOG(LogTemp, Log,
            TEXT("VideoEncoderWrapper: ffmpeg exited with code %d"), (int32)ExitCode);

        CloseHandle(Handle->hProcess);
        Handle->hProcess = nullptr;
    }

    // Close stderr log file handle.
    if (Handle->hStderrFile && Handle->hStderrFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Handle->hStderrFile);
        Handle->hStderrFile = nullptr;
    }

    delete Handle;
}