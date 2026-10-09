/**
 * @file OpenCirtPlugin.h
 * @brief BRX-Einstiegspunkt des openCirt-Plugins
 */

#ifndef OPENCIRTPLUGIN_H
#define OPENCIRTPLUGIN_H

#include "AcRx/AcRxObject.h"

// BRX API Version definition if not defined
#ifndef ACRX_APIVERSION
    #define ACRX_APIVERSION 19
#endif

// Platform-specific export macro
#ifdef _MSC_VER
    #define BRX_EXPORT __declspec(dllexport)
#else
    #define BRX_EXPORT __attribute__((visibility("default")))
#endif

// Entry point declaration - provided by drx_entrypoint.lib calls this
extern "C" BRX_EXPORT AcRx::AppRetCode
acrxEntryPoint(AcRx::AppMsgCode msg, void* pAppId);

// acrxGetApiVersion is provided by drx_entrypoint.lib - do NOT redeclare

#endif // OPENCIRTPLUGIN_H
