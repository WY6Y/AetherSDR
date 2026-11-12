#include "ExtAmpMeterApplet.h"

#include "AmpAppletStyles.h"
#include "CrossNeedleMeterApplet.h"
#include "core/AppSettings.h"
#include "core/LogManager.h"
#include "core/MqttClient.h"
#include "core/MqttSettings.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QVBoxLayout>

#include <cmath>

namespace AetherSDR {

namespace {

constexpr const char* kSettingsKey = "ExtAmpMeter";

QString formatWatts(double w)
{
    return w < 10.0 ? QString::number(w, 'f', 1) : QString::number(w, 'f', 0);
}

} // namespace

ExtAmpMeterApplet::ExtAmpMeterApplet(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 4);
    layout->setSpacing(3);

    auto* header = new QWidget(this);
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    m_title = new QLabel(header);
    m_title->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));
    m_statePill = new QLabel(header);
    m_lamp = new QLabel(header);
    row->addWidget(m_title);
    row->addStretch(1);
    row->addWidget(m_lamp);
    row->addWidget(m_statePill);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QWidget::customContextMenuRequested, this,
            [this, header](const QPoint& pos) { showContextMenu(header->mapTo(this, pos)); });
    layout->addWidget(header);

    // The same cross-needle face, theme menu and ballistics as the radio's PWR
    // applet; only the feed differs.
    m_needles = new CrossNeedleMeterApplet(this);
    layout->addWidget(m_needles, 1);

    m_readout = new QLabel(this);
    m_readout->setAlignment(Qt::AlignCenter);
    m_readout->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_readout, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) { showContextMenu(m_readout->mapTo(this, pos)); });
    layout->addWidget(m_readout);

    loadSettings();
    m_needles->setPowerScale(m_maxWatts, false);

    m_staleTimer.setInterval(500);
    connect(&m_staleTimer, &QTimer::timeout, this, &ExtAmpMeterApplet::checkStale);
    m_staleTimer.start();
    refresh();
}

void ExtAmpMeterApplet::setMqttClient(MqttClient* client)
{
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
    }
    m_client = client;
    if (m_client) {
        connect(m_client, &MqttClient::messageReceived, this, &ExtAmpMeterApplet::onMessage);
        connect(m_client, &MqttClient::disconnected, this, [this] {
            m_sensorStatus.clear();
            m_stale = true;
            refresh();
        });
    }
}

QStringList ExtAmpMeterApplet::subscriptionTopics() const
{
    if (m_root.isEmpty()) {
        return {};
    }
    return {m_root + QStringLiteral("meter"), m_root + QStringLiteral("status"),
            QString::fromLatin1(kRadioStateTopic)};
}

void ExtAmpMeterApplet::setFloating(bool floating)
{
    m_needles->setFloating(floating);
}

void ExtAmpMeterApplet::onMessage(const QString& topic, const QByteArray& payload)
{
    if (topic == QLatin1String(kRadioStateTopic)) {
        onRadioState(payload);
        return;
    }
    if (m_root.isEmpty() || !topic.startsWith(m_root)) {
        return;
    }
    const QStringView leaf = QStringView(topic).mid(m_root.size());
    if (leaf == QLatin1String("status")) {
        m_sensorStatus = QString::fromUtf8(payload).trimmed();
        refresh();
        return;
    }
    if (leaf != QLatin1String("meter")) {
        return;
    }
    const ExtAmpMeterReading r = parseExtAmpMeterReading(payload);
    if (!r.valid) {
        qCWarning(lcMqtt) << "ExtAmpMeter: ignoring malformed reading on" << topic;
        return;
    }
    // A sequence number that does not advance is a replayed or stuck message,
    // not a live reading (a retained meter would arrive exactly like this).
    if (r.haveSeq && m_haveReading && m_reading.haveSeq && r.seq == m_reading.seq) {
        return;
    }
    m_reading = r;
    m_haveReading = true;
    m_stale = false;
    m_sinceReading.restart();
    refresh();
}

void ExtAmpMeterApplet::onRadioState(const QByteArray& payload)
{
    const QJsonObject obj = QJsonDocument::fromJson(payload).object();
    const QJsonValue freq = obj.value(QStringLiteral("freq"));
    const QString band = freq.isDouble() ? extAmpMeterBandForMHz(freq.toDouble()) : QString();
    if (band != m_band) {
        m_band = band;
        refresh();
    }
}

void ExtAmpMeterApplet::checkStale()
{
    if (!m_stale && m_sinceReading.isValid() && m_sinceReading.elapsed() > kStaleMs) {
        m_stale = true;
        refresh();
    }
}

double ExtAmpMeterApplet::bandFactor() const
{
    return m_band.isEmpty() ? 1.0 : m_bandFactors.value(m_band, 1.0);
}

void ExtAmpMeterApplet::refresh()
{
    m_title->setText(m_name.isEmpty() ? tr("AMP") : m_name);

    if (m_root.isEmpty()) {
        m_statePill->setText(tr("NOT SET UP"));
        m_statePill->setStyleSheet(ampPillStyle(AmpPillState::Neutral));
        m_lamp->hide();
        m_needles->setTransmitting(false);
        m_readout->setText(tr("Right-click to set the sensor's MQTT topic root"));
        return;
    }

    const bool offline = m_sensorStatus == QLatin1String("offline");
    const bool live = m_haveReading && !m_stale && !offline;
    if (offline) {
        m_statePill->setText(tr("OFFLINE"));
        m_statePill->setStyleSheet(ampPillStyle(AmpPillState::Neutral));
    } else if (!live) {
        m_statePill->setText(tr("NO DATA"));
        m_statePill->setStyleSheet(ampPillStyle(AmpPillState::Neutral));
    } else if (m_reading.haveKeyed && m_reading.keyed) {
        m_statePill->setText(tr("TX"));
        m_statePill->setStyleSheet(ampPillStyle(AmpPillState::OperateTx));
    } else {
        m_statePill->setText(tr("LIVE"));
        m_statePill->setStyleSheet(ampPillStyle(AmpPillState::Standby));
    }

    // The lamp reflects the sensor's own protection output, and only when the
    // sensor publishes one. A stale "SWR OK" is worse than no lamp.
    const bool haveLamp = live && (m_reading.haveTripped || m_reading.haveOk);
    m_lamp->setVisible(haveLamp);
    if (haveLamp) {
        const bool tripped = (m_reading.haveTripped && m_reading.tripped) ||
                             (m_reading.haveOk && !m_reading.ok && !m_reading.haveTripped);
        QString text = tripped ? tr("TRIPPED") : tr("SWR OK");
        if (m_reading.haveTrips && m_reading.trips > 0) {
            text += tr(" · %1").arg(m_reading.trips);
        }
        m_lamp->setText(text);
        m_lamp->setStyleSheet(ampPillStyle(tripped ? AmpPillState::OperateTx
                                                   : AmpPillState::OperateRx));
    }

    if (!live) {
        m_needles->setTransmitting(false);
        m_readout->setText(m_band.isEmpty() ? QStringLiteral("—")
                                            : QStringLiteral("— · %1").arg(m_band));
        return;
    }

    // The correction scales both powers alike, so it never moves the SWR.
    const double k = bandFactor();
    const double fwd = m_reading.forwardWatts * k;
    const double ref = m_reading.haveReflected ? m_reading.reflectedWatts * k : 0.0;
    const bool active = (m_reading.haveKeyed && m_reading.keyed) || fwd >= 0.5;
    m_needles->setTransmitting(active);
    if (m_reading.haveReflected) {
        m_needles->setTxPowers(static_cast<float>(fwd), static_cast<float>(ref));
    } else {
        m_needles->setTxMeters(static_cast<float>(fwd),
                               static_cast<float>(m_reading.haveSwr ? m_reading.swr : 1.0));
    }

    QString text = tr("%1 W").arg(formatWatts(fwd));
    if (m_reading.haveReflected) {
        text += tr("  ·  ref %1 W").arg(QString::number(ref, 'f', ref < 10.0 ? 2 : 1));
    }
    if (m_reading.haveSwr) {
        text += tr("  ·  SWR %1").arg(QString::number(m_reading.swr, 'f', 2));
    }
    if (!m_band.isEmpty()) {
        text += QStringLiteral("  ·  %1").arg(m_band);
        if (!qFuzzyCompare(k, 1.0)) {
            text += QStringLiteral(" ×%1").arg(QString::number(k, 'f', 3));
        }
    }
    m_readout->setText(text);
}

void ExtAmpMeterApplet::showContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    menu.addAction(tr("MQTT topic root…"), this, [this] {
        bool ok = false;
        QString root = QInputDialog::getText(
            this, tr("External amp meter"),
            tr("Topic root (the sensor publishes <root>meter and <root>status):"),
            QLineEdit::Normal, m_root, &ok).trimmed();
        if (!ok) {
            return;
        }
        if (!root.isEmpty() && !root.endsWith(QLatin1Char('/'))) {
            root += QLatin1Char('/');
        }
        if (root == m_root) {
            return;
        }
        m_root = root;
        m_haveReading = false;
        m_stale = true;
        m_sensorStatus.clear();
        saveSettings();
        refresh();
        emit subscriptionTopicsChanged();
    });
    menu.addAction(tr("Display name…"), this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("External amp meter"), tr("Name:"),
                                                   QLineEdit::Normal, m_name, &ok).trimmed();
        if (ok) {
            m_name = name;
            saveSettings();
            refresh();
        }
    });
    menu.addAction(tr("Full scale (W)…"), this, [this] {
        bool ok = false;
        const int w = QInputDialog::getInt(this, tr("External amp meter"),
                                           tr("Amplifier full-scale power (W):"), m_maxWatts,
                                           1, 5000, 10, &ok);
        if (ok) {
            m_maxWatts = w;
            m_needles->setPowerScale(m_maxWatts, false);
            saveSettings();
        }
    });
    QAction* bandAct = menu.addAction(
        m_band.isEmpty() ? tr("Power correction (tune a band first)")
                         : tr("Power correction for %1…").arg(m_band),
        this, [this] {
            bool ok = false;
            const double k = QInputDialog::getDouble(
                this, tr("External amp meter"),
                tr("Multiply %1 readings by (true W ÷ shown W):").arg(m_band),
                bandFactor(), 0.5, 2.0, 3, &ok);
            if (!ok) {
                return;
            }
            if (qFuzzyCompare(k, 1.0)) {
                m_bandFactors.remove(m_band);
            } else {
                m_bandFactors.insert(m_band, k);
            }
            saveSettings();
            refresh();
        });
    bandAct->setEnabled(!m_band.isEmpty());
    menu.exec(mapToGlobal(pos));
}

void ExtAmpMeterApplet::loadSettings()
{
    const QJsonObject obj = QJsonDocument::fromJson(
        AppSettings::instance().value(QLatin1String(kSettingsKey)).toString().toUtf8()).object();
    m_root = obj.value(QStringLiteral("root")).toString();
    m_name = obj.value(QStringLiteral("name")).toString();
    const int w = obj.value(QStringLiteral("maxWatts")).toInt(kDefaultMaxWatts);
    m_maxWatts = w > 0 ? w : kDefaultMaxWatts;
    m_bandFactors.clear();
    const QJsonObject factors = obj.value(QStringLiteral("bandFactors")).toObject();
    for (auto it = factors.begin(); it != factors.end(); ++it) {
        const double k = it.value().toDouble(1.0);
        if (std::isfinite(k) && k >= 0.5 && k <= 2.0) {
            m_bandFactors.insert(it.key(), k);
        }
    }
}

void ExtAmpMeterApplet::saveSettings() const
{
    QJsonObject factors;
    for (auto it = m_bandFactors.cbegin(); it != m_bandFactors.cend(); ++it) {
        factors.insert(it.key(), it.value());
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("root"), m_root);
    obj.insert(QStringLiteral("name"), m_name);
    obj.insert(QStringLiteral("maxWatts"), m_maxWatts);
    obj.insert(QStringLiteral("bandFactors"), factors);
    auto& settings = AppSettings::instance();
    settings.setValue(QLatin1String(kSettingsKey),
                      QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    settings.save();
}

} // namespace AetherSDR
