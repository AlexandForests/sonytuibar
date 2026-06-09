#pragma once
#include <mdr-c/Connection.h>
#include <string>
#include <vector>

namespace tui
{
    struct DeviceEntry
    {
        std::string name;
        std::string mac;
    };

    /**
     * @brief Owns a platform-specific MDR connection backend and wraps the C connection API.
     * @note  All access goes through one thread (the pump thread) once connected.
     */
    class Connection
    {
    public:
        Connection(); // Creates the platform backend (macOS/Linux).
        ~Connection();

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        [[nodiscard]] bool valid() const { return mConn != nullptr; }
        [[nodiscard]] MDRConnection* raw() const { return mConn; }

        std::vector<DeviceEntry> ListDevices() const;
        int Connect(const std::string& mac, const char* serviceUUID) const; // MDR_RESULT_*
        int Poll(int timeoutMs) const; // MDR_RESULT_*
        void Disconnect() const;
        [[nodiscard]] const char* LastError() const;

    private:
        void* mBackend = nullptr; // Opaque MDRConnection<Platform>*
        MDRConnection* mConn = nullptr;
    };
}
