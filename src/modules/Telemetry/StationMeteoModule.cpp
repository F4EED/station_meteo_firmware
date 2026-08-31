#include "configuration.h"

#ifdef STATION_METEO

#include "StationMeteoModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "main.h"

#include <string.h>

StationMeteoModule::StationMeteoModule() : SinglePortModule("stmet", meshtastic_PortNum_PRIVATE_APP)
{
    loopbackOk = true;
    StationMeteoPrefsStore::instance().load();
}

bool StationMeteoModule::handlePayload(const uint8_t *bytes, size_t size)
{
    auto &store = StationMeteoPrefsStore::instance();
    store.load();

    if (size >= 8) {
        uint32_t magic = 0;
        memcpy(&magic, bytes, sizeof(magic));
        if (magic != StationMeteoPrefsStore::MAGIC)
            return false;
        const uint8_t cmd = bytes[5];
        if (cmd == STMET_CMD_GET) {
            replyPrefs(STMET_CMD_DATA);
            return true;
        }
        if ((cmd == STMET_CMD_SET || cmd == STMET_CMD_DATA) && size >= sizeof(StationMeteoPrefs)) {
            StationMeteoPrefs incoming{};
            memcpy(&incoming, bytes, sizeof(incoming));
            if (cmd == STMET_CMD_SET) {
                if (!store.apply(incoming))
                    return false;
            }
            replyPrefs(STMET_CMD_DATA);
            return true;
        }
    }
    return false;
}

void StationMeteoModule::replyPrefs(uint8_t cmd)
{
    auto blob = StationMeteoPrefsStore::instance().copyForWire(cmd);
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p)
        return;
    p->decoded.payload.size = sizeof(blob);
    memcpy(p->decoded.payload.bytes, &blob, sizeof(blob));
    p->to = nodeDB->getNodeNum();
    p->decoded.want_response = false;
    service->sendToPhone(p);
}

ProcessMessage StationMeteoModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    if (mp.which_payload_variant != meshtastic_MeshPacket_decoded_tag)
        return ProcessMessage::CONTINUE;
    if (handlePayload(mp.decoded.payload.bytes, mp.decoded.payload.size))
        return ProcessMessage::STOP;
    return ProcessMessage::CONTINUE;
}

meshtastic_MeshPacket *StationMeteoModule::allocReply()
{
    auto blob = StationMeteoPrefsStore::instance().copyForWire(STMET_CMD_DATA);
    auto reply = allocDataPacket();
    if (!reply)
        return nullptr;
    reply->decoded.payload.size = sizeof(blob);
    memcpy(reply->decoded.payload.bytes, &blob, sizeof(blob));
    return reply;
}

#endif
