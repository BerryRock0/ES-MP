class NetworkClient
{
public:
    bool Connect(const std::string &host, uint16_t port);
    void Disconnect();
    void Poll();

    void SendInput(
        int steering,
        bool thrust,
        bool fire);

    bool IsConnected() const;

private:
    // UDP/TCP socket
    // receive buffer
    // connection state
};
