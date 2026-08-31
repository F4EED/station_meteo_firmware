#include "configuration.h"

#ifdef STATION_METEO

#include "StationMeteoPrefs.h"
#include "FSCommon.h"
#include "SafeFile.h"
#include "SPILock.h"

StationMeteoPrefsStore &StationMeteoPrefsStore::instance()
{
    static StationMeteoPrefsStore store;
    return store;
}

StationMeteoPrefs StationMeteoPrefsStore::defaults()
{
    StationMeteoPrefs s{};
    s.magic = MAGIC;
    s.version = VERSION;
    s.cmd = STMET_CMD_STORE;
    s.reserved = 0;

    s.temp = {15.f, -10.f, 40.f, -20.f, 0.f, 30.f, 45.f};
    s.humidity = {50.f, 20.f, 90.f, 10.f, 20.f, 80.f, 95.f};
    s.pressure = {1013.f, 980.f, 1040.f, 960.f, 980.f, 1030.f, 1050.f};
    s.gasOhm = {100000.f, 5000.f, 1e7f, 1000.f, 5000.f, 5e6f, 2e7f};
    s.iaq = {50.f, 0.f, 150.f, 0.f, 25.f, 100.f, 200.f};
    s.co2 = {450.f, 400.f, 1000.f, 350.f, 400.f, 1200.f, 2000.f};

    finalize(s, STMET_CMD_STORE);
    return s;
}

uint32_t StationMeteoPrefsStore::computeHash(const StationMeteoPrefs &s)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(&s);
    const size_t n = (sizeof(StationMeteoPrefs) / sizeof(uint32_t)) - 1;
    uint32_t h = 0;
    for (size_t i = 0; i < n; i++)
        h ^= w[i];
    return h;
}

bool StationMeteoPrefsStore::valid(const StationMeteoPrefs &s)
{
    return s.magic == MAGIC && s.version == VERSION && s.xorHash == computeHash(s);
}

void StationMeteoPrefsStore::finalize(StationMeteoPrefs &s, uint8_t cmd)
{
    s.magic = MAGIC;
    s.version = VERSION;
    s.cmd = cmd;
    s.reserved = 0;
    s.xorHash = computeHash(s);
}

void StationMeteoPrefsStore::load()
{
    if (loaded)
        return;
    loaded = true;
    prefs = defaults();
#ifdef FSCom
    StationMeteoPrefs onDisk{};
    bool haveBlob = false;
    spiLock->lock();
    auto file = FSCom.open(kFileName, FILE_O_READ);
    if (file) {
        haveBlob = file.read((uint8_t *)&onDisk, sizeof(onDisk)) == sizeof(onDisk);
        file.close();
    }
    spiLock->unlock();
    if (haveBlob && valid(onDisk)) {
        onDisk.cmd = STMET_CMD_STORE;
        finalize(onDisk, STMET_CMD_STORE);
        prefs = onDisk;
        LOG_INFO("Station météo prefs restored from %s", kFileName);
    } else if (haveBlob) {
        LOG_WARN("Station météo prefs in %s rejected, using defaults", kFileName);
    } else {
        LOG_INFO("No station météo prefs (%s), using defaults", kFileName);
    }
#else
    LOG_ERROR("Filesystem not implemented");
#endif
}

bool StationMeteoPrefsStore::save()
{
#ifdef FSCom
    finalize(prefs, STMET_CMD_STORE);
    auto file = SafeFile(kFileName, true);
    file.write((uint8_t *)&prefs, sizeof(prefs));
    if (file.close()) {
        LOG_INFO("Station météo prefs written to %s", kFileName);
        return true;
    }
    LOG_WARN("Can't write station météo prefs (%s)", kFileName);
    return false;
#else
    LOG_ERROR("Filesystem not implemented");
    return false;
#endif
}

bool StationMeteoPrefsStore::apply(const StationMeteoPrefs &incoming)
{
    if (!valid(incoming))
        return false;
    prefs = incoming;
    finalize(prefs, STMET_CMD_STORE);
    return save();
}

StationMeteoPrefs StationMeteoPrefsStore::copyForWire(uint8_t cmd) const
{
    StationMeteoPrefs out = prefs;
    finalize(out, cmd);
    return out;
}

#endif
