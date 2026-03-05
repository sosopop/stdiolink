#pragma once

#include <QObject>
#include <QJsonObject>
#include <QTimer>

class SimPlcCraneDevice : public QObject {
public:
    struct Config {
        int cylinderUpDelayMs = 2500;
        int cylinderDownDelayMs = 2000;
        int valveOpenDelayMs = 1500;
        int valveCloseDelayMs = 1200;
    };

    enum class CylinderState { AtBottom, MovingUp, AtTop, MovingDown, Idle };
    enum class ValveState { Closed, MovingOpen, Open, MovingClose, Idle };

    explicit SimPlcCraneDevice(const Config& cfg, QObject* parent = nullptr);

    bool writeHoldingRegister(quint16 address, quint16 value, QString& err);

    QJsonObject snapshot() const;

    quint16 holdingRegister(quint16 address) const;
    CylinderState cylinderState() const { return m_cylinderState; }
    ValveState valveState() const { return m_valveState; }

    static QString cylinderStateName(CylinderState state);
    static QString valveStateName(ValveState state);

private:
    bool applyCylinderAction(quint16 value, QString& err);
    bool applyValveAction(quint16 value, QString& err);
    bool applyRunAction(quint16 value, QString& err);
    bool applyModeAction(quint16 value, QString& err);

    bool diCylinderUp() const;
    bool diCylinderDown() const;
    bool diValveOpen() const;
    bool diValveClosed() const;

    Config m_cfg;

    QTimer m_cylinderTimer;
    QTimer m_valveTimer;

    CylinderState m_cylinderState = CylinderState::AtBottom;
    ValveState m_valveState = ValveState::Closed;

    quint16 m_hrCylinder = 0;
    quint16 m_hrValve = 0;
    quint16 m_hrRun = 0;
    quint16 m_hrMode = 0;
};
