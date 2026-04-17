#include "Video/VideoEncoderWrapper.h"
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

    // Build command line.
    // Using libx264 (software) instead of h264_nvenc to avoid driver version issues.
    // At 1280x720@30fps the CPU cost is negligible.
    FString CmdLine = FString::Printf(
        TEXT("\"%s\" -y -loglevel info -f rawvideo -pix_fmt yuv420p -s %dx%d -r %d -i - "
            "-c:v libx264 -preset fast -crf 23 -pix_fmt yuv420p "
            "-movflags +faststart \"%s\""),
        *FfmpegPath, Width, Height, FPS, *OutputPath);

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
        TEXT("VideoEncoderWrapper: ffmpeg started (PID=%d, %dx%d @%dfps, libx264) -> %s"),
        (int32)ProcInfo.dwProcessId, Width, Height, FPS, *OutputPath);
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