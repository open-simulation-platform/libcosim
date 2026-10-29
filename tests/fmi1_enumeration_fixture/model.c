#include <FMI1/fmiFunctions.h>

#include <stdlib.h>

typedef struct
{
    fmiCallbackFreeMemory freeMemory;
    fmiInteger input;
    fmiInteger output;
    fmiInteger integer;
    fmiInteger status;
} fixture;

const char* fmiGetTypesPlatform(void) { return fmiPlatform; }
const char* fmiGetVersion(void) { return fmiVersion; }
fmiStatus fmiSetDebugLogging(fmiComponent c, fmiBoolean enabled)
{
    (void)c; (void)enabled; return fmiOK;
}

fmiComponent fmiInstantiateSlave(
    fmiString name, fmiString guid, fmiString location, fmiString mime,
    fmiReal timeout, fmiBoolean visible, fmiBoolean interactive,
    fmiCallbackFunctions callbacks, fmiBoolean logging)
{
    (void)name; (void)guid; (void)location; (void)mime; (void)timeout;
    (void)visible; (void)interactive; (void)logging;
    fixture* instance = callbacks.allocateMemory(1, sizeof(fixture));
    if (!instance) return NULL;
    instance->freeMemory = callbacks.freeMemory;
    instance->input = 1;
    instance->output = 1;
    return instance;
}

void fmiFreeSlaveInstance(fmiComponent c)
{
    fixture* instance = (fixture*)c;
    if (instance) instance->freeMemory(instance);
}

fmiStatus fmiInitializeSlave(fmiComponent c, fmiReal start, fmiBoolean stopDefined, fmiReal stop)
{
    (void)c; (void)start; (void)stopDefined; (void)stop; return fmiOK;
}
fmiStatus fmiTerminateSlave(fmiComponent c) { (void)c; return fmiOK; }
fmiStatus fmiResetSlave(fmiComponent c)
{
    fixture* instance = (fixture*)c;
    instance->input = 1; instance->output = 1;
    instance->integer = 0; instance->status = 0; return fmiOK;
}
fmiStatus fmiDoStep(fmiComponent c, fmiReal time, fmiReal step, fmiBoolean newStep)
{
    fixture* instance = (fixture*)c;
    (void)time; (void)step; (void)newStep;
    instance->output = instance->input;
    return fmiOK;
}

fmiStatus fmiGetInteger(
    fmiComponent c, const fmiValueReference vr[], size_t count, fmiInteger values[])
{
    fixture* instance = (fixture*)c;
    size_t i;
    for (i = 0; i < count; ++i) {
        switch (vr[i]) {
            case 1: values[i] = instance->input; break;
            case 2: values[i] = instance->output; break;
            case 3: values[i] = instance->integer; break;
            case 4: values[i] = instance->status; break;
            default: return fmiError;
        }
    }
    if (instance->status == 1) return fmiWarning;
    if (instance->status == 2) return fmiDiscard;
    if (instance->status == 3) return fmiError;
    return fmiOK;
}

fmiStatus fmiSetInteger(
    fmiComponent c, const fmiValueReference vr[], size_t count, const fmiInteger values[])
{
    fixture* instance = (fixture*)c;
    size_t i;
    for (i = 0; i < count; ++i) {
        switch (vr[i]) {
            case 1:
                if (instance->status == 2) return fmiDiscard;
                if (instance->status == 3) return fmiError;
                instance->input = values[i];
                break;
            case 3: instance->integer = values[i]; break;
            case 4: instance->status = values[i]; break;
            default: return fmiError;
        }
    }
    return instance->status == 1 ? fmiWarning : fmiOK;
}

#define UNSUPPORTED_GET(NAME, TYPE) \
    fmiStatus NAME(fmiComponent c, const fmiValueReference vr[], size_t n, TYPE values[]) \
    { (void)c; (void)vr; (void)n; (void)values; return fmiError; }
#define UNSUPPORTED_SET(NAME, TYPE) \
    fmiStatus NAME(fmiComponent c, const fmiValueReference vr[], size_t n, const TYPE values[]) \
    { (void)c; (void)vr; (void)n; (void)values; return fmiError; }
UNSUPPORTED_GET(fmiGetReal, fmiReal)
UNSUPPORTED_GET(fmiGetBoolean, fmiBoolean)
UNSUPPORTED_GET(fmiGetString, fmiString)
UNSUPPORTED_SET(fmiSetReal, fmiReal)
UNSUPPORTED_SET(fmiSetBoolean, fmiBoolean)
UNSUPPORTED_SET(fmiSetString, fmiString)

fmiStatus fmiSetRealInputDerivatives(fmiComponent c, const fmiValueReference vr[],
    size_t n, const fmiInteger order[], const fmiReal values[])
{ (void)c; (void)vr; (void)n; (void)order; (void)values; return fmiError; }
fmiStatus fmiGetRealOutputDerivatives(fmiComponent c, const fmiValueReference vr[],
    size_t n, const fmiInteger order[], fmiReal values[])
{ (void)c; (void)vr; (void)n; (void)order; (void)values; return fmiError; }
fmiStatus fmiCancelStep(fmiComponent c) { (void)c; return fmiError; }
#define UNSUPPORTED_STATUS(NAME, TYPE) \
    fmiStatus NAME(fmiComponent c, fmiStatusKind kind, TYPE* value) \
    { (void)c; (void)kind; (void)value; return fmiError; }
UNSUPPORTED_STATUS(fmiGetStatus, fmiStatus)
UNSUPPORTED_STATUS(fmiGetRealStatus, fmiReal)
UNSUPPORTED_STATUS(fmiGetIntegerStatus, fmiInteger)
UNSUPPORTED_STATUS(fmiGetBooleanStatus, fmiBoolean)
UNSUPPORTED_STATUS(fmiGetStringStatus, fmiString)
