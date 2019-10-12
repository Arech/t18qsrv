/*
    This file is a part of t18qsrv project (proxy-server plugin for QUIK terminal)
    Copyright (C) 2019, Arech (aradvert@gmail.com; https://github.com/Arech)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

//min() and max() triggers very weird compiler crash while doing ::std::numeric_limits<vec_len_t>::max()
#define NOMINMAX

//\vc\include\yvals.h(112): error C2338: /RTCc rejects conformant code, so it isn't supported by the C++ Standard Library.
//Either remove this compiler option, or define _ALLOW_RTCc_IN_STL to acknowledge that you have received this warning.
#define _ALLOW_RTCc_IN_STL

#define _USE_MATH_DEFINES // for C++ math constants

/*
#ifndef WIN32
#error("Quik supports only x86 .dlls. Change platform to compile")
#endif
*/

//////////////////////////////////////////////////////////////////////////
//some common QLua stuff

#define LUA_LIB
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define LUA_BUILD_AS_DLL
#endif

#include <chrono>
#include <thread>

#include <qluacpp/qlua>

/*
#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <Windows.h>*/


//////////////////////////////////////////////////////////////////////////
// we need lua.lib
#pragma comment(lib,"lua5.1.lib")

#include "../t18/t18/debug.h"
