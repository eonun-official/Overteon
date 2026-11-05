#pragma once
// ============================================================
//  Overteon Qt/QML UI — QML type registration.
//
//  The two painted items are registered under "Overteon 1.0" so the QML
//  files can `import Overteon 1.0` (both the plugin editor and the preview
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

    qmlRegisterType<OvEqView>   ("Overteon", 1, 0, "OvEqView");
    qmlRegisterType<OvMeterView>("Overteon", 1, 0, "OvMeterView");
}
