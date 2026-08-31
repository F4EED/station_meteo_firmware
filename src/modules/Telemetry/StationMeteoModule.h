#pragma once

#ifdef STATION_METEO

#include "SinglePortModule.h"
#include "StationMeteoPrefs.h"

// Port officiel PRIVATE_APP : GET/SET du blob /prefs/station_meteo.dat (pas de proto).
class StationMeteoModule : public SinglePortModule
{
  public:
    StationMeteoModule();

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual meshtastic_MeshPacket *allocReply() override;

  private:
    void replyPrefs(uint8_t cmd);
    bool handlePayload(const uint8_t *bytes, size_t size);
};

#endif
