class NetworkClient
{
public:
	using SnapshotHandler =	std::function<void(const NetworkSnapshot &)>;

	explicit NetworkClient(SnapshotHandler handler);

    bool Connect(const std::string &host, uint16_t port);
    void Disconnect();
    void Poll();

    void SendInput(
        int steering,
        bool thrust,
        bool fire);

    bool IsConnected() const;

private:
	SnapshotHandler snapshotHandler;
};
