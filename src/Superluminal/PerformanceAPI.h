/*
BSD LICENSE

Copyright (c) 2019-2020 Superluminal. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once

#include "PerformanceAPI_capi.h"
#include "PerformanceAPI_loader.h"

// When PERFORMANCEAPI_ENABLED is defined to 0, all calls to the PerformanceAPI (either through macro or direct function calls) will be compiled out.
#ifndef PERFORMANCEAPI_ENABLED
    #ifdef _WIN32
        #define	PERFORMANCEAPI_ENABLED 1
    #else
        #define	PERFORMANCEAPI_ENABLED 0
    #endif
#endif

namespace PerformanceAPI
{
#if PERFORMANCEAPI_ENABLED
    struct InstrumentationScope final
    {
        static PerformanceAPI_Functions    s_functions;
        static PerformanceAPI_ModuleHandle s_module;

        static void initialize(const wchar_t* inPathToDLL);
        static void deinitialize();

        InstrumentationScope(const char* inID);
        InstrumentationScope(const char* inID, const char* inData);
        InstrumentationScope(const char* inID, uint16_t inIDLength, const char* inData, uint16_t inDataLength);
        InstrumentationScope(const char* inID, const char* inData, uint32_t inColor);
        InstrumentationScope(const wchar_t* inID);
        InstrumentationScope(const wchar_t* inID, const wchar_t* inData);
        InstrumentationScope(const wchar_t* inID, const wchar_t* inData, uint32_t inColor);
        ~InstrumentationScope();
    };

    // Private helper macros
    #define PERFORMANCEAPI_CAT_IMPL(a, b) a##b
    #define PERFORMANCEAPI_CAT(a, b) PERFORMANCEAPI_CAT_IMPL(a, b)
    #define PERFORMANCEAPI_UNIQUE_IDENTIFIER(a) PERFORMANCEAPI_CAT(a, __LINE__)

    #define PERFORMANCEAPI_INSTRUMENT(InstrumentationID)							PerformanceAPI::InstrumentationScope PERFORMANCEAPI_UNIQUE_IDENTIFIER(__instrumentation_scope__)((InstrumentationID));
    #define PERFORMANCEAPI_INSTRUMENT_DATA(InstrumentationID, InstrumentationData)	PerformanceAPI::InstrumentationScope PERFORMANCEAPI_UNIQUE_IDENTIFIER(__instrumentation_scope__)((InstrumentationID), (InstrumentationData));
    #define PERFORMANCEAPI_INSTRUMENT_DATA_N(InstrumentationID, InstrumentationIDLength, InstrumentationData, InstrumentationDataLength)	PerformanceAPI::InstrumentationScope PERFORMANCEAPI_UNIQUE_IDENTIFIER(__instrumentation_scope__)((InstrumentationID), (InstrumentationIDLength), (InstrumentationData), (InstrumentationDataLength));
    #define PERFORMANCEAPI_INSTRUMENT_COLOR(InstrumentationID, InstrumentationColor)	PerformanceAPI::InstrumentationScope PERFORMANCEAPI_UNIQUE_IDENTIFIER(__instrumentation_scope__)((InstrumentationID), "", (InstrumentationColor));
    #define PERFORMANCEAPI_INSTRUMENT_DATA_COLOR(InstrumentationID, InstrumentationData, InstrumentationColor)	PerformanceAPI::InstrumentationScope PERFORMANCEAPI_UNIQUE_IDENTIFIER(__instrumentation_scope__)((InstrumentationID), (InstrumentationData), (InstrumentationColor));
    #define PERFORMANCEAPI_INSTRUMENT_FUNCTION()									PERFORMANCEAPI_INSTRUMENT(__FUNCTION__)
    #define PERFORMANCEAPI_INSTRUMENT_FUNCTION_DATA(InstrumentationData)			PERFORMANCEAPI_INSTRUMENT_DATA(__FUNCTION__, (InstrumentationData))
    #define PERFORMANCEAPI_INSTRUMENT_FUNCTION_COLOR(InstrumentationColor)			PERFORMANCEAPI_INSTRUMENT_COLOR(__FUNCTION__, (InstrumentationColor))
    #define PERFORMANCEAPI_INSTRUMENT_FUNCTION_DATA_COLOR(InstrumentationData, InstrumentationColor) PERFORMANCEAPI_INSTRUMENT_DATA_COLOR(__FUNCTION__, (InstrumentationData), (InstrumentationColor))

#else
    #define PERFORMANCEAPI_INSTRUMENT(InstrumentationID)
    #define PERFORMANCEAPI_INSTRUMENT_DATA(InstrumentationID, InstrumentationData)
    #define PERFORMANCEAPI_INSTRUMENT_DATA_N(InstrumentationID, InstrumentationIDLength, InstrumentationData, InstrumentationDataLength)
    #define PERFORMANCEAPI_INSTRUMENT_COLOR(InstrumentationID, InstrumentationColor)
    #define PERFORMANCEAPI_INSTRUMENT_DATA_COLOR(InstrumentationID, InstrumentationData, InstrumentationColor)

    #define PERFORMANCEAPI_INSTRUMENT_FUNCTION()
    #define PERFORMANCEAPI_INSTRUMENT_FUNCTION_DATA(InstrumentationData)
    #define PERFORMANCEAPI_INSTRUMENT_FUNCTION_COLOR(InstrumentationColor)
    #define PERFORMANCEAPI_INSTRUMENT_FUNCTION_DATA_COLOR(InstrumentationData, InstrumentationColor)
#endif
}
