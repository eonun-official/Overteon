#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — QML type registration.
//
//  The two painted items are registered under "OnyxTEON 1.0" so the QML
//  files can `import OnyxTEON 1.0` (both the plugin editor and the preview
//  app call this once, before loading main.qml).
// ============================================================
#include <QtQml/qqml.h>

#include "OvEqView.h"
#include "OvMeterView.h"

inline void ovRegisterQmlTypes()
{
    static bool done = false;
    if (done) return;
    done = true;

    qmlRegisterType<OvEqView>   ("OnyxTEON", 1, 0, "OvEqView");
    qmlRegisterType<OvMeterView>("OnyxTEON", 1, 0, "OvMeterView");
}
