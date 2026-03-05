#include "sim_device.h"

#include <QString>

SimPlcCraneDevice::SimPlcCraneDevice(const Config& cfg, QObject* parent)
    : QObject(parent), m_cfg(cfg) {
    m_cylinderTimer.setSingleShot(true);
    m_valveTimer.setSingleShot(true);

    QObject::connect(&m_cylinderTimer, &QTimer::timeout, this, [this]() {
        if (m_cylinderState == CylinderState::MovingUp) {
            m_cylinderState = CylinderState::AtTop;
            return;
        }
        if (m_cylinderState == CylinderState::MovingDown) {
            m_cylinderState = CylinderState::AtBottom;
        }
    });

    QObject::connect(&m_valveTimer, &QTimer::timeout, this, [this]() {
        if (m_valveState == ValveState::MovingOpen) {
            m_valveState = ValveState::Open;
            return;
        }
        if (m_valveState == ValveState::MovingClose) {
            m_valveState = ValveState::Closed;
        }
    });
}

bool SimPlcCraneDevice::writeHoldingRegister(quint16 address, quint16 value, QString& err) {
    switch (address) {
    case 0:
        return applyCylinderAction(value, err);
    case 1:
        return applyValveAction(value, err);
    case 2:
        return applyRunAction(value, err);
    case 3:
        return applyModeAction(value, err);
    default:
        err = QString("Unsupported holding register address: %1").arg(address);
        return false;
    }
}

QJsonObject SimPlcCraneDevice::snapshot() const {
    QJsonObject out;
    out["hr_cylinder"] = static_cast<int>(m_hrCylinder);
    out["hr_valve"] = static_cast<int>(m_hrValve);
    out["hr_run"] = static_cast<int>(m_hrRun);
    out["hr_mode"] = static_cast<int>(m_hrMode);

    out["cylinder_state"] = cylinderStateName(m_cylinderState);
    out["valve_state"] = valveStateName(m_valveState);

    out["di_cylinder_up"] = diCylinderUp();
    out["di_cylinder_down"] = diCylinderDown();
    out["di_valve_open"] = diValveOpen();
    out["di_valve_closed"] = diValveClosed();
    return out;
}

quint16 SimPlcCraneDevice::holdingRegister(quint16 address) const {
    switch (address) {
    case 0:
        return m_hrCylinder;
    case 1:
        return m_hrValve;
    case 2:
        return m_hrRun;
    case 3:
        return m_hrMode;
    default:
        return 0;
    }
}

QString SimPlcCraneDevice::cylinderStateName(CylinderState state) {
    switch (state) {
    case CylinderState::AtBottom:
        return "at_bottom";
    case CylinderState::MovingUp:
        return "moving_up";
    case CylinderState::AtTop:
        return "at_top";
    case CylinderState::MovingDown:
        return "moving_down";
    case CylinderState::Idle:
        return "idle";
    }
    return "unknown";
}

QString SimPlcCraneDevice::valveStateName(ValveState state) {
    switch (state) {
    case ValveState::Closed:
        return "closed";
    case ValveState::MovingOpen:
        return "moving_open";
    case ValveState::Open:
        return "open";
    case ValveState::MovingClose:
        return "moving_close";
    case ValveState::Idle:
        return "idle";
    }
    return "unknown";
}

bool SimPlcCraneDevice::applyCylinderAction(quint16 value, QString& err) {
    if (value > 2) {
        err = QString("Invalid cylinder action: %1, expected 0/1/2").arg(value);
        return false;
    }

    m_hrCylinder = value;

    if (value == 0) {
        m_cylinderTimer.stop();
        if (m_cylinderState == CylinderState::MovingUp ||
            m_cylinderState == CylinderState::MovingDown) {
            m_cylinderState = CylinderState::Idle;
        }
        return true;
    }

    if (value == 1) {
        if (m_cylinderState == CylinderState::AtTop ||
            m_cylinderState == CylinderState::MovingUp) {
            return true;
        }
        m_cylinderTimer.stop();
        m_cylinderState = CylinderState::MovingUp;
        m_cylinderTimer.start(m_cfg.cylinderUpDelayMs);
        return true;
    }

    if (m_cylinderState == CylinderState::AtBottom ||
        m_cylinderState == CylinderState::MovingDown) {
        return true;
    }
    m_cylinderTimer.stop();
    m_cylinderState = CylinderState::MovingDown;
    m_cylinderTimer.start(m_cfg.cylinderDownDelayMs);
    return true;
}

bool SimPlcCraneDevice::applyValveAction(quint16 value, QString& err) {
    if (value > 2) {
        err = QString("Invalid valve action: %1, expected 0/1/2").arg(value);
        return false;
    }

    m_hrValve = value;

    if (value == 0) {
        m_valveTimer.stop();
        if (m_valveState == ValveState::MovingOpen ||
            m_valveState == ValveState::MovingClose) {
            m_valveState = ValveState::Idle;
        }
        return true;
    }

    if (value == 1) {
        if (m_valveState == ValveState::Open ||
            m_valveState == ValveState::MovingOpen) {
            return true;
        }
        m_valveTimer.stop();
        m_valveState = ValveState::MovingOpen;
        m_valveTimer.start(m_cfg.valveOpenDelayMs);
        return true;
    }

    if (m_valveState == ValveState::Closed ||
        m_valveState == ValveState::MovingClose) {
        return true;
    }
    m_valveTimer.stop();
    m_valveState = ValveState::MovingClose;
    m_valveTimer.start(m_cfg.valveCloseDelayMs);
    return true;
}

bool SimPlcCraneDevice::applyRunAction(quint16 value, QString& err) {
    if (value > 1) {
        err = QString("Invalid run action: %1, expected 0/1").arg(value);
        return false;
    }
    m_hrRun = value;
    return true;
}

bool SimPlcCraneDevice::applyModeAction(quint16 value, QString& err) {
    if (value > 1) {
        err = QString("Invalid mode action: %1, expected 0/1").arg(value);
        return false;
    }
    m_hrMode = value;
    return true;
}

bool SimPlcCraneDevice::diCylinderUp() const {
    return m_cylinderState == CylinderState::AtTop;
}

bool SimPlcCraneDevice::diCylinderDown() const {
    return m_cylinderState == CylinderState::AtBottom;
}

bool SimPlcCraneDevice::diValveOpen() const {
    return m_valveState == ValveState::Open;
}

bool SimPlcCraneDevice::diValveClosed() const {
    return m_valveState == ValveState::Closed;
}
