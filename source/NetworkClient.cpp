// NetworkClient.cpp
void NetworkClient::HandlePacket(const uint8_t *data, size_t size)
{
    NetworkSnapshot snapshot;

    if(!NetworkSnapshot::Deserialize(data, size, snapshot))
        return;

    if(snapshotHandler)
        snapshotHandler(snapshot);
}
