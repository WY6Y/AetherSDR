#include "ExtAmpMeterReading.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>

namespace AetherSDR {

namespace {

// A JSON number or a 0/1 integer reads as a bool; anything else is "absent".
bool readFlag(const QJsonObject& obj, const char* key, bool* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isBool()) {
        *out = v.toBool();
        return true;
    }
    if (v.isDouble()) {
        *out = v.toDouble() != 0.0;
        return true;
    }
    return false;
}

bool readNumber(const QJsonObject& obj, const char* key, double* out)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (!v.isDouble() || !std::isfinite(v.toDouble())) {
        return false;
    }
    *out = v.toDouble();
    return true;
}

} // namespace

ExtAmpMeterReading parseExtAmpMeterReading(const QByteArray& payload)
{
    ExtAmpMeterReading r;
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        return r;
    }
    const QJsonObject obj = doc.object();

    double fwd = 0.0;
    if (!readNumber(obj, "fwd_w", &fwd) || fwd < 0.0) {
        return r;
    }
    r.valid = true;
    r.forwardWatts = fwd;

    double v = 0.0;
    if (readNumber(obj, "ref_w", &v) && v >= 0.0) {
        r.haveReflected = true;
        r.reflectedWatts = v;
    }
    if (readNumber(obj, "swr", &v) && v >= 1.0) {
        r.haveSwr = true;
        r.swr = v;
    }
    r.haveKeyed = readFlag(obj, "keyed", &r.keyed);
    r.haveOk = readFlag(obj, "ok", &r.ok);
    r.haveTripped = readFlag(obj, "tripped", &r.tripped);
    if (readNumber(obj, "trips", &v) && v >= 0.0) {
        r.haveTrips = true;
        r.trips = static_cast<qint64>(v);
    }
    if (readNumber(obj, "seq", &v)) {
        r.haveSeq = true;
        r.seq = static_cast<qint64>(v);
    }
    return r;
}

QString extAmpMeterBandForMHz(double mhz)
{
    struct Band { double lo, hi; const char* name; };
    static constexpr Band kBands[] = {
        {1.800, 2.000, "160m"}, {3.500, 4.000, "80m"},   {5.330, 5.410, "60m"},
        {7.000, 7.300, "40m"},  {10.100, 10.150, "30m"}, {14.000, 14.350, "20m"},
        {18.068, 18.168, "17m"}, {21.000, 21.450, "15m"}, {24.890, 24.990, "12m"},
        {28.000, 29.700, "10m"}, {50.000, 54.000, "6m"},
    };
    for (const Band& b : kBands) {
        if (mhz >= b.lo && mhz <= b.hi) {
            return QString::fromLatin1(b.name);
        }
    }
    return QString();
}

} // namespace AetherSDR
