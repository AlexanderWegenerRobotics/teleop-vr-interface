// Deliberately NO #pragma once. This header is pure #undef, so re-including it
// is both harmless and the point: any file that pulls <Windows.h> in again
// should be able to cap it again without an include guard swallowing the call.
//
// Include immediately after anything that drags in <Windows.h> -- directly, or
// transitively via <msgpack.hpp>, which includes <winsock2.h> on MSVC for
// ntohl/htonl.
//
// AllowWindowsPlatformTypes/HideWindowsPlatformTypes deal with *types* -- they
// save and restore INT, DWORD, TRUE and friends. They do not touch the A/W
// function macros that winbase.h and winuser.h define:
//
//     #define UpdateResource  UpdateResourceW
//     #define GetObject       GetObjectW
//     #define DrawText        DrawTextW
//     ...
//
// Those macros survive to the end of the translation unit. Under a unity build
// the translation unit is not one .cpp but every .cpp concatenated into the
// same Module.teleop_vr_interface.N.cpp, so a file compiled after this one
// inherits them. UTexture2D::UpdateResource() then expands to
// UpdateResourceW(), and a file that never mentioned Windows fails to compile:
//
//     LocalPreviewSource.cpp(58): error C2039:
//         'UpdateResourceW': is not a member of 'UTexture2D'
//
// What makes this nasty is that the victim moves. UBT compiles recently-edited
// files on their own and reshuffles whatever is left into the unity blobs, so
// editing an unrelated file is enough to change who sits downwind of the
// macros. The fix therefore belongs with the file that DEFINES them, not with
// whichever file happened to be next in line this time.
//
// Only names that collide with UE API are undef'd. Windows code that genuinely
// wants one of these calls the explicit ...W form (CreateFileW, CreateProcessW),
// which is unaffected -- and is already the convention in this module.
//
// #undef of a macro that is not defined is a no-op, so this is safe to include
// unconditionally.

#undef UpdateResource
#undef GetObject
#undef DrawText
#undef DrawTextEx
#undef CreateFile
#undef DeleteFile
#undef MoveFile
#undef CopyFile
#undef CreateDirectory
#undef RemoveDirectory
#undef CreateProcess
#undef GetMessage
#undef SendMessage
#undef PostMessage
#undef LoadString
#undef GetClassName
#undef GetCurrentTime
#undef PlaySound
#undef SetPort
#undef GetEnvironmentVariable
#undef ExpandEnvironmentStrings
#undef OutputDebugString
#undef GetComputerName
#undef GetUserName
#undef GetFileAttributes
#undef SetFileAttributes
#undef GetTempPath
#undef GetFullPathName
#undef FindWindow
#undef MessageBox
