#include "Video/VideoEncoderWrapper.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
// Windows A/W macros leak through the unity build; all Windows calls below use the
// explicit ...W forms. See WindowsMacroCleanup.h.
#include "Shared/WindowsMacroCleanup.h"

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
// VideoLogger runs two 720p30 encodes at once, which in software is the largest CPU
// consumer in the process, so hardware encoders are tried first.
// Candidates are probed in order with the exact argument string used for recording;
// NVENC option names differ between ffmpeg versions. NVENC needs a driver matching
// the bundled ffmpeg's SDK; h264_mf (MediaFoundation, hw_encoding=true) is the
// fallback that reaches the same hardware through the OS.
struct FEncoderCandidate { const TCHAR* Name; const TCHAR* Args; };

static const FEncoderCandidate kHwCandidates[] = {
    { TEXT("h264_nvenc"), TEXT("-c:v h264_nvenc -preset p4 -tune ll -rc vbr -cq 23 -b:v 0 -pix_fmt yuv420p") },
    { TEXT("h264_nvenc"), TEXT("-c:v h264_nvenc -preset llhq -rc vbr_hq -cq 23 -b:v 0 -pix_fmt yuv420p") },
    { TEXT("h264_nvenc"), TEXT("-c:v h264_nvenc -b:v 8M -pix_fmt yuv420p") },
    { TEXT("h264_mf"),    TEXT("-c:v h264_mf -hw_encoding true -b:v 8M -pix_fmt yuv420p") },
    { TEXT("h264_mf"),    TEXT("-c:v h264_mf -b:v 8M -pix_fmt yuv420p") },
};
// Software fallback. -threads 4 caps x264's worker threads (default ~1.5x logical
// cores per encoder), which otherwise shows up as frame-time jitter. veryfast is
// ~2-3x cheaper than fast at the same CRF.
static const TCHAR* kX264Args = TEXT("-c:v libx264 -preset veryfast -crf 23 -threads 4 -pix_fmt yuv420p");

static TAutoConsoleVariable<int32> CVarVideoLogEncoder(
    TEXT("teleop.VideoLog.Encoder"),
    0,
    TEXT("Encoder for VideoLogger's mp4 output. 0 = auto (probe hardware encoders, else x264), ")
    TEXT("1 = prefer hardware, 2 = force software x264."),
    ECVF_Default);

// Runs ffmpeg to completion and captures stdout/stderr, draining while it runs so a
// full pipe cannot deadlock the child.
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

// Short real encode with the given arguments; exit code 0 means this build and
// machine accept them. The failure reason is logged, since the exit code alone does
// not distinguish them.
static bool TryEncoderArgs(const TCHAR* Args)
{
    // 256x256 because NVENC rejects very small frames on some driver/SDK
    // combinations. -nostdin is required: CreateProc leaves the child's stdin handle
    // NULL, and ffmpeg's keyboard handling would make every probe fail.
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

// Encoder arguments and name, honouring the CVar override. Cached, since it depends
// on the machine and the ffmpeg build only.
static const TCHAR* SelectEncoderArgs(const TCHAR*& OutName)
{
    const int32 Override = CVarVideoLogEncoder.GetValueOnAnyThread();
    if (Override == 2) { OutName = TEXT("libx264 (forced)"); return kX264Args; }

    // Lives for the whole editor process: a Live Coding patch does not reset it, so
    // restart the editor to re-probe.
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

            // Control probe: if x264 fails too, the probe harness is at fault, not
            // NVENC.
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