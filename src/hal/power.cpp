#include "power.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

#include "pins.h"

namespace power {
namespace {

constexpr uint32_t kReadMs = 5000;
constexpr uint8_t kGaugeCtrlReg = 0x18;  // bit 3 turns the fuel gauge on
constexpr uint8_t kGaugeBit = 3;
constexpr uint16_t kPowerOffMv = 3100;  // 2600..3300 in 100 mV steps

// Low leaves a little while to find a cable (a Li-ion cell drops fast below
// 3.5 V); Critical stays well above the chip's own cut-off, to save and
// switch off cleanly rather than lose the power mid-write
constexpr uint16_t kLowMv = 3450;
constexpr uint16_t kLowBackMv = 3550;  // Ok again above this
constexpr uint16_t kCriticalMv = 3300;
constexpr uint16_t kCriticalBackMv = 3400;  // Low again above this (pretend() goes back up)
constexpr uint8_t kConfirmReads = 3;

XPowersPMU pmu;
bool online = false;
Battery current;
Readings extra;
uint32_t rev = 0;
uint32_t lastRead = 0;
uint8_t lowReads = 0;  // readings in a row at or below kLowMv
uint8_t criticalReads = 0;
uint16_t pretendMv = 0;

// The chip's gauge counts 4.2 V as full; charged to 4.1 it stops short
// (96% for the 1000 mAh cell). So what it reads when the charger says it's
// done is 100%, learned again at every full charge (a new cell too), kept in
// NVS and written only when it changes.
constexpr char kNamespace[] = "power";
constexpr int kFullMin = 70;  // a "done" below this is something else (a fault, a cell going): not learned
int fullPoint = 100;

void learnFull(int gauge) {
    if (gauge < kFullMin || gauge > 100 || gauge == fullPoint) return;
    fullPoint = gauge;
    Preferences prefs;
    prefs.begin(kNamespace, false);
    prefs.putUChar("full", fullPoint);
    prefs.end();
    Serial.printf("power: full is %d%% on the gauge\n", fullPoint);
}

int scaled(int gauge) { return min(100, (gauge * 100 + fullPoint / 2) / fullPoint); }

// A rough single-cell Li-ion curve, for when the gauge has no answer yet;
// full at 4.1 V, where the charger stops (setChargeTargetVoltage below)
int percentFromVoltage(uint16_t mv) {
    static const uint16_t kMv[] = {3300, 3500, 3600, 3700, 3750, 3800, 3900, 4000, 4100};
    static const uint8_t kPct[] = {0, 5, 10, 22, 33, 45, 66, 83, 100};
    if (mv <= kMv[0]) return 0;
    for (size_t i = 1; i < sizeof(kPct); i++) {
        if (mv <= kMv[i]) return kPct[i - 1] + (kPct[i] - kPct[i - 1]) * (mv - kMv[i - 1]) / (kMv[i] - kMv[i - 1]);
    }
    return 100;
}

// Down only after kConfirmReads readings in a row, up as soon as one is past the margin
Level levelFor(const Battery &b) {
    if (b.usb || !b.present || b.millivolts == 0) {
        lowReads = criticalReads = 0;
        return Level::Ok;
    }
    auto count = [&](uint8_t &reads, uint16_t mv) {
        reads = b.millivolts > mv ? 0 : min<uint8_t>(reads + 1, kConfirmReads);
    };
    count(lowReads, kLowMv);
    count(criticalReads, kCriticalMv);
    Level level = current.level;
    if (criticalReads >= kConfirmReads) {
        level = Level::Critical;
    } else if (lowReads >= kConfirmReads && level == Level::Ok) {
        level = Level::Low;
    }
    if (level == Level::Critical && b.millivolts > kCriticalBackMv) level = Level::Low;
    if (level == Level::Low && b.millivolts > kLowBackMv) level = Level::Ok;
    return level;
}

void read() {
    Battery b;
    if (pretendMv) {
        b.present = true;
        b.millivolts = pretendMv;
        b.percent = percentFromVoltage(pretendMv);
    } else {
        b.present = pmu.isBatteryConnect();
        b.usb = pmu.isVbusIn();
        b.charging = b.present && pmu.isCharging();
        if (b.present) {
            b.millivolts = pmu.getBattVoltage();
            const int gauge = pmu.getBatteryPercent();
            if (gauge >= 0 && gauge <= 100) {
                b.gauge = gauge;
                if (b.usb && pmu.getChargerStatus() == XPOWERS_AXP2101_CHG_DONE_STATE) learnFull(gauge);
                b.percent = scaled(gauge);
            } else {
                b.percent = percentFromVoltage(b.millivolts);
            }
        }
    }
    if (online) {
        extra.usbMv = pmu.isVbusIn() ? pmu.getVbusVoltage() : 0;
        extra.chipC = pmu.getTemperature();
    }
    b.level = levelFor(b);
    if (b.present != current.present || b.charging != current.charging || b.usb != current.usb ||
        b.percent != current.percent || b.level != current.level) {
        rev++;
    }
    if (b.level != current.level) {
        static const char *const kNames[] = {"ok", "low", "critical"};
        Serial.printf("power: battery %s at %u mV\n", kNames[static_cast<int>(b.level)], b.millivolts);
    }
    current = b;
}

}  // namespace

bool begin() {
    {
        Preferences prefs;
        prefs.begin(kNamespace, false);  // read-only fails (and logs) before the first save
        const int full = prefs.getUChar("full", 100);
        prefs.end();
        if (full >= kFullMin && full <= 100) fullPoint = full;
    }
    online = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
    if (!online) {
        Serial.println("power: AXP2101 not found, no battery level or PWR button");
        return false;
    }
    // Only the button's edges; holding it 10 s still powers off (the chip does that itself)
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ | XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_10S);
    // Off well before the cell is truly empty (the chip's default is 2.6 V,
    // which wears it); the level already reads 0% at 3.3 V
    pmu.setSysPowerDownVoltage(kPowerOffMv);
    // 0.5C for the 1000 mAh cell: the usual LiPo rate, about 2 h to full
    // (the chip's 200 mA took 5); it slows down by itself if it gets hot
    pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);
    // Mostly on USB, so the cell sits full for months: 4.1 V instead of 4.2
    // wears it much less, for about 10% less time on battery
    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V1);
    // And then stop: at C/8 of the 1000 mAh cell the charger says done and
    // lets go, charging again only once the cell has dropped a little. Set
    // here, not left to the chip: its registers outlive the firmware (the cell
    // keeps it powered), and once they had termination off, so it held the
    // cell at 4.1 V forever and never said done (nor showed 100%)
    pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_125MA);
    pmu.enableChargerTerminationLimit();
    // Room for the radio at full volume and the charger at once, from any USB port that has it
    pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_1500MA);

    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
    pmu.enableTemperatureMeasure();
    pmu.setRegisterBit(kGaugeCtrlReg, kGaugeBit);
    read();
    lastRead = millis();
    Serial.printf("power: battery %s, %d%%, %u mV%s\n", current.present ? "present" : "absent", current.percent,
                  current.millivolts, current.charging ? ", charging" : "");
    return true;
}

void loop() {
    if (!online || millis() - lastRead < kReadMs) return;
    lastRead = millis();
    read();
}

const Battery &battery() { return current; }
int fullAt() { return fullPoint; }
const Readings &readings() { return extra; }
uint32_t revision() { return rev; }

void shutdown() {
    if (!online) return;
    if (pretendMv) {
        Serial.println("power: would switch off now, but the battery is pretend (B0 for the real one)");
        return;
    }
    Serial.println("power: switching off");
    pmu.shutdown();
}

void pretend(uint16_t mv) {
    pretendMv = mv;
    if (!online) return;
    read();
    lastRead = millis();
}

uint16_t pretending() { return pretendMv; }

ChargeSettings chargeSettings() {
    ChargeSettings s;
    if (!online) return s;
    // Register codes to units, as in XPowersLib's enums
    static const uint16_t kTargetMv[] = {0, 4000, 4100, 4200, 4350, 4400};
    static const uint16_t kUsbLimitMa[] = {100, 500, 900, 1000, 1500, 2000};
    const uint8_t target = pmu.getChargeTargetVoltage();
    if (target < sizeof(kTargetMv) / sizeof(kTargetMv[0])) s.targetMv = kTargetMv[target];
    const uint8_t cc = pmu.getChargerConstantCurr();  // 25 mA steps up to 200, then 100 mA steps
    s.currentMa = cc <= 8 ? cc * 25 : 200 + (cc - 8) * 100;
    s.stopMa = pmu.getChargerTerminationCurr() * 25;
    s.stopOn = pmu.isChargerTerminationLimit();
    s.cutoffMv = pmu.getSysPowerDownVoltage();
    const uint8_t usb = pmu.getVbusCurrentLimit();
    if (usb < sizeof(kUsbLimitMa) / sizeof(kUsbLimitMa[0])) s.usbLimitMa = kUsbLimitMa[usb];
    return s;
}

void readKeyEdges(bool &pressed, bool &released) {
    pressed = released = false;
    if (!online) return;
    pmu.getIrqStatus();
    pressed = pmu.isPekeyNegativeIrq();  // PWRON is pulled low while held
    released = pmu.isPekeyPositiveIrq();
    if (pressed || released) pmu.clearIrqStatus();
}

}  // namespace power
