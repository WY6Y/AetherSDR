#pragma once

#include <QByteArray>
#include <QString>

namespace AetherSDR {

// Payload contract for an external station power sensor published over MQTT,
// read by ExtAmpMeterApplet. Written for CyberAmp (a Pico on a Stockton
// tandem-match coupler after the amplifier), but nothing here is specific to
// it: any sensor that publishes the same JSON drives the same meter.
//
//   <root>meter   {"fwd_w":48.2,"ref_w":0.51,"swr":1.22,"keyed":1,
//                  "ok":1,"tripped":0,"trips":0,"seq":1234, ...}
//   <root>status  "online" | "offline"   (retained, the sensor's LWT)
//
// Only fwd_w is required. Every other field is keyed by PRESENCE, the same rule
// as aethersdr/radio/state: an absent `tripped` means "this sensor has no trip
// output", never "not tripped", and the applet shows no lamp rather than a
// green one it was never told about.
struct ExtAmpMeterReading {
    bool valid{false};          // fwd_w present and finite

    double forwardWatts{0.0};
    bool haveReflected{false};
    double reflectedWatts{0.0};
    bool haveSwr{false};
    double swr{1.0};

    bool haveKeyed{false};
    bool keyed{false};
    bool haveOk{false};         // the sensor's own "OK to transmit" output
    bool ok{false};
    bool haveTripped{false};
    bool tripped{false};
    bool haveTrips{false};
    qint64 trips{0};
    bool haveSeq{false};
    qint64 seq{0};
};

// Parse one <root>meter payload. Returns valid=false for anything that is not a
// JSON object with a finite, non-negative fwd_w.
ExtAmpMeterReading parseExtAmpMeterReading(const QByteArray& payload);

// Amateur band name ("20m") for a frequency in MHz, or an empty string outside
// the HF/6 m allocations. Used to key the per-band power correction.
QString extAmpMeterBandForMHz(double mhz);

} // namespace AetherSDR
