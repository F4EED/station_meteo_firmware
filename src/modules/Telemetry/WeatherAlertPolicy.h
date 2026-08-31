#pragma once

#ifdef STATION_METEO

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "NodeDB.h"
#include "StationMeteoPrefs.h"
#include "gps/RTC.h"
#include <math.h>
#include <stdint.h>
#include <time.h>

// Seuils 0.1.0 : constantes de repli si /prefs/station_meteo.dat absent.
// Priorité choc > alerte > normal.
// gas_resistance sur le fil = kOhm (BME680Sensor) ; prefs gaz en Ω.
struct WeatherAlertPolicy {
    static constexpr float TEMP_MIN = -10.f;
    static constexpr float TEMP_MAX = 40.f;
    static constexpr float TEMP_SHOCK = 3.f;
    static constexpr float HUM_MIN = 20.f;
    static constexpr float HUM_MAX = 90.f;
    static constexpr float HUM_SHOCK = 15.f;
    static constexpr float PRES_MIN = 980.f;
    static constexpr float PRES_MAX = 1040.f;
    static constexpr float PRES_SHOCK = 5.f;
    static constexpr float GAS_KOHM_MIN = 5.f;     // 5 000 Ω
    static constexpr float GAS_KOHM_MAX = 10000.f; // 1,0×10⁷ Ω
    static constexpr float GAS_SHOCK_REL = 0.30f;
    static constexpr uint16_t IAQ_MIN = 0;
    static constexpr uint16_t IAQ_MAX = 150;
    static constexpr uint16_t IAQ_SHOCK = 50;
    static constexpr float CO2_SHOCK = 200.f;

    static constexpr uint32_t INTERVAL_ALERT_S = 3600;
    static constexpr uint32_t INTERVAL_SHOCK_S = 300;
    static constexpr uint32_t INTERVAL_NORMAL_NO_RTC_S = 21600;
    static constexpr int SLOT_HOURS[3] = {6, 12, 18};

    enum class Mode : uint8_t { Normal = 0, Alert = 1, Shock = 2 };

    static const StationMeteoPrefs &prefs()
    {
        auto &store = StationMeteoPrefsStore::instance();
        store.load();
        return store.get();
    }

    static uint32_t secondsUntilNextSlot(int hour, int minute, int second)
    {
        const int now = hour * 3600 + minute * 60 + second;
        for (int s : SLOT_HOURS) {
            const int t = s * 3600;
            if (t > now)
                return (uint32_t)(t - now);
        }
        return (uint32_t)((24 * 3600 - now) + SLOT_HOURS[0] * 3600);
    }

    static bool outOfRange(bool has, float value, float mini, float maxi)
    {
        return has && (value < mini || value > maxi);
    }

    static bool shocked(bool hasPrev, bool hasNow, float prev, float now, float absDelta)
    {
        return hasPrev && hasNow && fabsf(now - prev) >= absDelta;
    }

    static uint32_t apply(const meshtastic_EnvironmentMetrics &m)
    {
        static bool havePrev = false;
        static meshtastic_EnvironmentMetrics prev = meshtastic_EnvironmentMetrics_init_zero;

        const StationMeteoPrefs &p = prefs();
        const float tempMin = p.temp.low;
        const float tempMax = p.temp.high;
        const float humMin = p.humidity.low;
        const float humMax = p.humidity.high;
        const float presMin = p.pressure.low;
        const float presMax = p.pressure.high;
        const float gasKohmMin = p.gasOhm.low / 1000.f;
        const float gasKohmMax = p.gasOhm.high / 1000.f;
        const float iaqMin = p.iaq.low;
        const float iaqMax = p.iaq.high;
        const float co2Min = p.co2.low;
        const float co2Max = p.co2.high;

        Mode mode = Mode::Normal;

        const bool shockTemp = shocked(havePrev && prev.has_temperature, m.has_temperature, prev.temperature, m.temperature,
                                       TEMP_SHOCK);
        const bool shockHum = shocked(havePrev && prev.has_relative_humidity, m.has_relative_humidity, prev.relative_humidity,
                                      m.relative_humidity, HUM_SHOCK);
        const bool shockPres =
            shocked(havePrev && prev.has_barometric_pressure, m.has_barometric_pressure, prev.barometric_pressure,
                    m.barometric_pressure, PRES_SHOCK);
        const bool shockGas = havePrev && prev.has_gas_resistance && m.has_gas_resistance && prev.gas_resistance > 0.f &&
                              fabsf(m.gas_resistance - prev.gas_resistance) / prev.gas_resistance >= GAS_SHOCK_REL;
        const bool shockIaq = havePrev && prev.has_iaq && m.has_iaq &&
                              ((m.iaq > prev.iaq ? m.iaq - prev.iaq : prev.iaq - m.iaq) >= IAQ_SHOCK);
        const bool shockCo2 = havePrev && prev.has_iaq && m.has_iaq &&
                              fabsf(StationMeteoPrefsStore::estimatedCo2Ppm(m.iaq) -
                                    StationMeteoPrefsStore::estimatedCo2Ppm(prev.iaq)) >= CO2_SHOCK;

        if (shockTemp || shockHum || shockPres || shockGas || shockIaq || shockCo2)
            mode = Mode::Shock;
        else if (outOfRange(m.has_temperature, m.temperature, tempMin, tempMax) ||
                 outOfRange(m.has_relative_humidity, m.relative_humidity, humMin, humMax) ||
                 outOfRange(m.has_barometric_pressure, m.barometric_pressure, presMin, presMax) ||
                 outOfRange(m.has_gas_resistance, m.gas_resistance, gasKohmMin, gasKohmMax) ||
                 (m.has_iaq && (float(m.iaq) < iaqMin || float(m.iaq) > iaqMax)) ||
                 (m.has_iaq &&
                  outOfRange(true, StationMeteoPrefsStore::estimatedCo2Ppm(m.iaq), co2Min, co2Max)))
            mode = Mode::Alert;

        uint32_t interval = INTERVAL_ALERT_S;
        if (mode == Mode::Shock) {
            interval = INTERVAL_SHOCK_S;
        } else if (mode == Mode::Normal) {
            const uint32_t now = getValidTime(RTCQualityFromNet, true);
            if (now == 0) {
                interval = INTERVAL_NORMAL_NO_RTC_S;
            } else {
                time_t t = (time_t)now;
                struct tm tm{};
                localtime_r(&t, &tm);
                interval = secondsUntilNextSlot(tm.tm_hour, tm.tm_min, tm.tm_sec);
                if (interval < 60)
                    interval = 60;
            }
        }

        moduleConfig.telemetry.environment_update_interval = interval;
        prev = m;
        havePrev = true;
        return interval;
    }
};

#endif
