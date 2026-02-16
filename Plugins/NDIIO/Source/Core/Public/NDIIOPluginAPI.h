/*
	Copyright (C) 2024 Vizrt NDI AB. All rights reserved.

	This file and it's use within a Product is bound by the terms of NDI SDK license that was provided
	as part of the NDI SDK. For more information, please review the license and the NDI SDK documentation.
*/

#pragma once

#include <CoreMinimal.h>

#include <vector>
#include <algorithm>
#include <functional>
#include <chrono>

#if PLATFORM_WINDOWS
#include <Windows/AllowWindowsPlatformTypes.h>
#endif

#ifndef NDI_SDK_ENABLED
#error NDI(R) 6.x Runtime must be installed for the NDI(R) IO plugin to run properly.
#endif

#ifdef NDI_SDK_ENABLED
#include <Processing.NDI.Lib.h>
#include <Processing.NDI.Lib.cplusplus.h>
#endif

#if PLATFORM_WINDOWS
#include <Windows/HideWindowsPlatformTypes.h>
#endif

#define NDIIO_MODULE_NAME FName(TEXT("NDIIO"))