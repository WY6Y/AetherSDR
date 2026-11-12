#pragma once

#include "core/ExtAmpMeterReading.h"

#include <QElapsedTimer>
#include <QMap>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QLabel;

namespace AetherSDR {

class CrossNeedleMeterApplet;
class MqttClient;

// Cross-needle forward/reflected power and SWR meter for an EXTERNAL power
// sensor that publishes over MQTT, typically a coupler after a linear
// amplifier. It sits beside the radio's own PWR meter rather than replacing
// it: the radio meter shows drive, this one shows what the amplifier actually
// puts into the line, and the operator wants both at once.
//
// Payload contract: core/ExtAmpMeterReading.h. Configuration (AppSettings key
// "ExtAmpMeter", client-owned): topic root, display name, full-scale watts and
// a per-band power correction. Right-click the header to edit them.
//
// Three rules this class exists to enforce:
//   1. A meter that stopped updating never looks live. With no <root>meter for
//      kStaleMs, or <root>status "offline", the needles park and the pill says
//      so; the last reading is not held on the face.
//   2. Only what the sensor sent is shown. No trip lamp unless it publishes
//      `tripped`/`ok`; reflected comes from `ref_w`, not derived.
//   3. The band (for the correction) comes from AetherSDR's own
//      aethersdr/radio/state over the same broker, so the applet needs no
//      MainWindow hook and follows whichever slice is active.
class ExtAmpMeterApplet : public QWidget {
    Q_OBJECT

public:
    explicit ExtAmpMeterApplet(QWidget* parent = nullptr);

    void setMqttClient(MqttClient* client);

    // Topics this applet needs subscribed, appended to the user's list when the
    // MQTT applet connects. Empty while no topic root is configured.
    QStringList subscriptionTopics() const;

    void setFloating(bool floating);

signals:
    // The topic root changed: the subscription list must be rebuilt.
    void subscriptionTopicsChanged();

private:
    static constexpr int kStaleMs = 2500;   // sensors publish at least 1 Hz idle
    static constexpr int kDefaultMaxWatts = 150;

    void onMessage(const QString& topic, const QByteArray& payload);
    void onRadioState(const QByteArray& payload);
    void checkStale();
    void refresh();
    void loadSettings();
    void saveSettings() const;
    void showContextMenu(const QPoint& pos);
    double bandFactor() const;

    MqttClient* m_client{nullptr};
    CrossNeedleMeterApplet* m_needles{nullptr};
    QLabel* m_title{nullptr};
    QLabel* m_statePill{nullptr};
    QLabel* m_lamp{nullptr};
    QLabel* m_readout{nullptr};
    QTimer m_staleTimer;
    QElapsedTimer m_sinceReading;

    // Settings
    QString m_root;
    QString m_name;
    int m_maxWatts{kDefaultMaxWatts};
    QMap<QString, double> m_bandFactors;

    // Live state
    ExtAmpMeterReading m_reading;
    bool m_haveReading{false};
    bool m_stale{true};
    QString m_sensorStatus;     // last <root>status payload, "" until seen
    QString m_band;             // from aethersdr/radio/state, "" when unknown
};

} // namespace AetherSDR
