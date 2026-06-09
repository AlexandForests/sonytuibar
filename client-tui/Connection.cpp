#include "Connection.hpp"

#if defined(__APPLE__)
#include <mdr-c/Platform/PlatformMacOS.h>
#elif defined(__linux__)
#include <mdr-c/Platform/PlatformLinux.h>
#endif

namespace tui
{
    Connection::Connection()
    {
#if defined(__APPLE__)
        auto* backend = mdrConnectionMacOSCreate();
        mBackend = backend;
        mConn = backend ? mdrConnectionMacOSGet(backend) : nullptr;
#elif defined(__linux__)
        auto* backend = mdrConnectionLinuxCreate();
        mBackend = backend;
        mConn = backend ? mdrConnectionLinuxGet(backend) : nullptr;
#endif
    }

    Connection::~Connection()
    {
        if (mConn)
            mdrConnectionDisconnect(mConn);
#if defined(__APPLE__)
        if (mBackend)
            mdrConnectionMacOSDestroy(static_cast<MDRConnectionMacOS*>(mBackend));
#elif defined(__linux__)
        if (mBackend)
            mdrConnectionLinuxDestroy(static_cast<MDRConnectionLinux*>(mBackend));
#endif
    }

    std::vector<DeviceEntry> Connection::ListDevices() const
    {
        std::vector<DeviceEntry> out;
        if (!mConn)
            return out;

        MDRDeviceInfo* list = nullptr;
        int count = 0;
        if (mdrConnectionGetDevicesList(mConn, &list, &count) != MDR_RESULT_OK)
            return out;

        out.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            out.push_back({list[i].szDeviceName, list[i].szDeviceMacAddress});

        mdrConnectionFreeDevicesList(mConn, &list);
        return out;
    }

    int Connection::Connect(const std::string& mac, const char* serviceUUID) const
    {
        if (!mConn)
            return MDR_RESULT_ERROR_NO_CONNECTION;
        return mdrConnectionConnect(mConn, mac.c_str(), serviceUUID);
    }

    int Connection::Poll(int timeoutMs) const
    {
        if (!mConn)
            return MDR_RESULT_ERROR_NO_CONNECTION;
        return mdrConnectionPoll(mConn, timeoutMs);
    }

    void Connection::Disconnect() const
    {
        if (mConn)
            mdrConnectionDisconnect(mConn);
    }

    const char* Connection::LastError() const
    {
        return mConn ? mdrConnectionGetLastError(mConn) : "No connection backend";
    }
}
