// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//
// Third generation by Oles.

#ifndef stdafxH
#define stdafxH

#pragma once

#include "../xrCore/xrCore.h"

#pragma warning(push)
#pragma warning(disable:4995)
#ifdef __linux__
#include <dplay/dplay8.h> // mingw-w64 header in Common/linux/include (the SDK one is MSVC-only)
#else
#include <DPlay/dplay8.h>
#endif
#pragma warning(pop)

#include "NET_Shared.h"

#define _RELEASE(x)			{ if(x) { (x)->Release();       (x)=NULL; } }
#define _SHOW_REF(msg, x)   { if(x) { x->AddRef(); Log(msg,u32(x->Release()));}}

#endif //stdafxH
