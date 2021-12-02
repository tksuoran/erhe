#include "PerformanceAPI.h"

namespace PerformanceAPI
{

PerformanceAPI_Functions    InstrumentationScope::s_functions{};
PerformanceAPI_ModuleHandle InstrumentationScope::s_module   {nullptr};

void InstrumentationScope::initialize(const wchar_t* inPathToDLL)
{
    s_module = PerformanceAPI_LoadFrom(inPathToDLL, &s_functions);
}

void InstrumentationScope::deinitialize()
{
    PerformanceAPI_Free(&s_module);
}

InstrumentationScope::InstrumentationScope(const char* inID)
{
    s_functions.BeginEvent(inID, nullptr, PERFORMANCEAPI_DEFAULT_COLOR);
}

InstrumentationScope::InstrumentationScope(const char* inID, const char* inData)
{
    s_functions.BeginEvent(inID, inData, PERFORMANCEAPI_DEFAULT_COLOR);
}

InstrumentationScope::InstrumentationScope(const char* inID, uint16_t inIDLength, const char* inData, uint16_t inDataLength)
{
    s_functions.BeginEventN(inID, inIDLength, inData, inDataLength, PERFORMANCEAPI_DEFAULT_COLOR);
}

InstrumentationScope::InstrumentationScope(const char* inID, const char* inData, uint32_t inColor)
{
    s_functions.BeginEvent(inID, inData, inColor);
}

InstrumentationScope::InstrumentationScope(const wchar_t* inID)
{
    s_functions.BeginEventWide(inID, nullptr, PERFORMANCEAPI_DEFAULT_COLOR);
}

InstrumentationScope::InstrumentationScope(const wchar_t* inID, const wchar_t* inData)
{
    s_functions.BeginEventWide(inID, inData, PERFORMANCEAPI_DEFAULT_COLOR);
}

InstrumentationScope::InstrumentationScope(const wchar_t* inID, const wchar_t* inData, uint32_t inColor)
{
    s_functions.BeginEventWide(inID, inData, inColor);
}

InstrumentationScope::~InstrumentationScope()
{
    s_functions.EndEvent();
}

} // namespace PerformanceAPI
