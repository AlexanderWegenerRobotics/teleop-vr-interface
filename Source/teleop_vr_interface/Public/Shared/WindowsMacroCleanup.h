// Undefines the Windows A/W function macros (UpdateResource, GetObject, DrawText,
// ...) that collide with UE API. Hide/AllowWindowsPlatformTypes only handle types,
// and under a unity build the macros leak into every .cpp compiled afterwards (e.g.
// UTexture2D::UpdateResource becoming UpdateResourceW).
// Include right after anything that pulls in <Windows.h>, including <msgpack.hpp> on
// MSVC. No #pragma once on purpose, so it can be re-applied. Windows code here uses
// the explicit ...W calls.

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
