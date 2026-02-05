#pragma once

#ifdef _WIN32
    #ifdef STDIOLINK_BUILDING_DLL
        #define STDIOLINK_API __declspec(dllexport)
    #else
        #define STDIOLINK_API __declspec(dllimport)
    #endif
#else
    #define STDIOLINK_API
#endif
