#pragma once
// ============================================================
//  OnyxTEON Qt/QML UI — one parameter, exposed to QML as an object.
// ============================================================
#include <QObject>
#include <QString>
#include <QStringList>

#include "OvSource.h"

class OvParam : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString id        READ id        CONSTANT)
    Q_PROPERTY(QString label     READ label     CONSTANT)
    Q_PROPERTY(bool    isChoice  READ isChoice  CONSTANT)
    Q_PROPERTY(bool    isBool    READ isBool    CONSTANT)
    Q_PROPERTY(QStringList choices READ choices CONSTANT)
    Q_PROPERTY(double  minValue  READ minValue  CONSTANT)
    Q_PROPERTY(double  maxValue  READ maxValue  CONSTANT)
    Q_PROPERTY(double  defaultValue READ defaultValue CONSTANT)

    Q_PROPERTY(double  norm      READ norm      NOTIFY valueChanged)
    Q_PROPERTY(double  value     READ value     NOTIFY valueChanged)
    Q_PROPERTY(QString display   READ display   NOTIFY displayChanged)
    Q_PROPERTY(bool    dragging  READ dragging  NOTIFY draggingChanged)

public:
    OvParam (OvSource& source, int index, QObject* parent = nullptr);

    QString id()        const { return m_id; }
    QString label()     const { return m_label; }
    bool    isChoice()  const { return ! m_spec.choices.empty(); }
    bool    isBool()    const { return m_spec.isBool; }
    QStringList choices() const { return m_choices; }
    double  minValue()  const { return m_spec.minValue; }
    double  maxValue()  const { return m_spec.maxValue; }
    double  defaultValue() const { return m_spec.defaultNorm; }

    double  norm()      const { return m_norm; }
    double  value()     const { return m_value; }
    QString display()   const { return m_display; }
    bool    dragging()  const { return m_dragging; }

    /// Refresh from the host parameter; emits only when something changed.
    void poll();

    Q_INVOKABLE void setNorm (double n);
    Q_INVOKABLE void setValue (double v);          // scaled value (Hz, dB, ...)
    Q_INVOKABLE void resetToDefault();
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();

    int index() const { return m_index; }

signals:
    void valueChanged();
    void displayChanged();
    void draggingChanged();
    void edited();                                  // user (not host) changed it

private:
    QString formatted (double norm) const;
    void    cache();

    OvSource&   m_source;
    int         m_index;
    OvParamSpec m_spec;
    QString     m_id, m_label;
    QStringList m_choices;
    double      m_norm = 0.0, m_value = 0.0;
    QString     m_display;
    bool        m_dragging = false;
    bool        m_haveCached = false;
};
