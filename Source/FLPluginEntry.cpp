#include "FLPlugin.h"

#if defined(_WIN32)
 #define PATTERN_BANK_EXPORT __declspec(dllexport)
#else
 #define PATTERN_BANK_EXPORT __attribute__((visibility("default")))
#endif

extern "C" PATTERN_BANK_EXPORT TFruityPlug* CreatePlugInstance (TFruityPlugHost* host, TPluginTag tag)
{
    try
    {
        return new stepshaper::FLPlugin (host, tag);
    }
    catch (...)
    {
        return nullptr;
    }
}
