// External amp meter (MQTT) payload contract: ExtAmpMeterReading.
//
// The fields are keyed by presence, so the cases that matter are the ones a
// naive parser gets plausibly wrong: an absent trip flag read as "not tripped",
// a missing fwd_w read as 0 W, and garbage read as a valid zero reading. The
// payload strings below are the exact shape CyberAmp v1.5.x publishes.

#include "core/ExtAmpMeterReading.h"

#include <iostream>

using namespace AetherSDR;

namespace {

bool expect(bool condition, const char* label)
{
    std::cout << (condition ? "[ OK ] " : "[FAIL] ") << label << '\n';
    return condition;
}

} // namespace

int main()
{
    bool ok = true;

    {
        const auto r = parseExtAmpMeterReading(
            R"({"seq":1730,"up_ms":173004,"fwd_w":10.0,"ref_w":0.00,"swr":1.00,"port":"B",)"
            R"("ok":1,"tripped":0,"trips":0,"keyed":1,"slow":0,"max_pass_us":1004,)"
            R"("drops":0,"pub_drops":0,"fw":"1.5.3","hold":0})");
        ok &= expect(r.valid, "CyberAmp payload is valid");
        ok &= expect(r.forwardWatts == 10.0 && r.haveReflected && r.reflectedWatts == 0.0,
                     "forward and reflected watts read");
        ok &= expect(r.haveKeyed && r.keyed, "keyed read from 1");
        ok &= expect(r.haveOk && r.ok && r.haveTripped && !r.tripped, "ok/tripped read");
        ok &= expect(r.haveSeq && r.seq == 1730, "seq read");
    }
    {
        const auto r = parseExtAmpMeterReading(
            R"({"fwd_w":3.1,"ref_w":1.08,"swr":3.87,"ok":0,"tripped":1,"trips":1,"keyed":1})");
        ok &= expect(r.valid && r.haveTripped && r.tripped && !r.ok && r.trips == 1,
                     "tripped reading");
        ok &= expect(r.haveSwr && r.swr > 3.8 && r.swr < 3.9, "swr read");
    }
    {
        const auto r = parseExtAmpMeterReading(R"({"fwd_w":42.5})");
        ok &= expect(r.valid && r.forwardWatts == 42.5, "fwd_w alone is enough");
        ok &= expect(!r.haveTripped && !r.haveOk && !r.haveReflected && !r.haveKeyed,
                     "absent fields stay absent, not false/0");
    }
    {
        const auto r = parseExtAmpMeterReading(R"({"fwd_w":true,"keyed":true})");
        ok &= expect(!r.valid, "boolean fwd_w rejected");
    }
    ok &= expect(!parseExtAmpMeterReading(R"({"ref_w":1.0})").valid, "missing fwd_w rejected");
    ok &= expect(!parseExtAmpMeterReading(R"({"fwd_w":-1})").valid, "negative fwd_w rejected");
    ok &= expect(!parseExtAmpMeterReading("online").valid, "status text is not a reading");
    ok &= expect(!parseExtAmpMeterReading("").valid, "empty payload rejected");
    {
        const auto r = parseExtAmpMeterReading(R"({"fwd_w":5,"swr":0.5,"ref_w":-2})");
        ok &= expect(r.valid && !r.haveSwr && !r.haveReflected,
                     "impossible swr/ref_w dropped, reading kept");
    }

    ok &= expect(extAmpMeterBandForMHz(14.074) == QStringLiteral("20m"), "14.074 is 20m");
    ok &= expect(extAmpMeterBandForMHz(3.916) == QStringLiteral("80m"), "3.916 is 80m");
    ok &= expect(extAmpMeterBandForMHz(28.0) == QStringLiteral("10m"), "band edge included");
    ok &= expect(extAmpMeterBandForMHz(14.5).isEmpty(), "out of band is empty");
    ok &= expect(extAmpMeterBandForMHz(0.0).isEmpty(), "no slice is empty");

    std::cout << (ok ? "ALL PASS" : "FAILURES") << '\n';
    return ok ? 0 : 1;
}
