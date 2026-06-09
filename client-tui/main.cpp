// SonyHeadphonesClient — btop-style terminal client (Phase 1: scaffold + connect)
//
// Threading model: everything (UI + libmdr + connection) runs on the main
// thread. This is deliberate — macOS IOBluetooth delivers its async RFCOMM
// callbacks on the main thread's run loop, and the platform backend's Poll()
// runs the *current* thread's run loop. So libmdr must be driven on the same
// (main) thread that owns the run loop. We use ftxui::Loop::RunOnce() (instead
// of the blocking ScreenInteractive::Loop) so the main thread can interleave
// rendering with servicing the connection. (Matches the reference client, which
// relies on SDL pumping the Cocoa run loop every frame.)
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <mdr/Headphones.hpp>
#include "Connection.hpp"
#include "Panels.hpp"

using namespace ftxui;

namespace
{
    enum class Stage
    {
        Picking,
        Connecting,
        Connected,
        Failed
    };

    // Run loop servicing window per pump step (ms). Also paces the main loop.
    constexpr int kPollMs = 15;

    struct App
    {
        Stage stage = Stage::Picking;
        std::string status;

        std::vector<std::string> deviceNames;
        std::vector<std::string> deviceMacs;
        int selected = 0;

        bool connectRequested = false; // set by the Connect button
        std::string requestedMac;

        // Poll-only values (battery) aren't pushed by the device; re-sync periodically.
        std::chrono::steady_clock::time_point lastSync{};
    };

    // How often to re-request poll-only state (battery) while connected.
    constexpr auto kSyncInterval = std::chrono::seconds(5);

    // One step of driving libmdr. Runs on the main thread; conn.Poll() services
    // the run loop so IOBluetooth callbacks (connect complete, incoming data) fire.
    void PumpStep(tui::Connection& conn, mdr::MDRHeadphones& device, App& app)
    {
        try
        {
            switch (app.stage)
            {
            case Stage::Picking:
                if (app.connectRequested)
                {
                    app.connectRequested = false;
                    app.stage = Stage::Connecting;
                    app.status = "Connecting...";
                    int res = conn.Connect(app.requestedMac, MDR_SERVICE_UUID_XM5);
                    if (res != MDR_RESULT_OK && res != MDR_RESULT_INPROGRESS)
                    {
                        app.stage = Stage::Failed;
                        app.status = conn.LastError();
                    }
                }
                break;

            case Stage::Connecting:
            {
                int res = conn.Poll(kPollMs); // services run loop; OK once handshake completes
                if (res == MDR_RESULT_OK)
                {
                    device = mdr::MDRHeadphones(conn.raw());
                    device.Invoke(device.RequestInitV2());
                    app.stage = Stage::Connected;
                    app.status = "Connected";
                }
                else if (res != MDR_RESULT_INPROGRESS && res != MDR_RESULT_ERROR_TIMEOUT)
                {
                    conn.Disconnect();
                    app.stage = Stage::Failed;
                    app.status = conn.LastError();
                }
                break;
            }

            case Stage::Connected:
            {
                // Service the run loop so incoming RFCOMM data reaches the recv buffer.
                if (conn.Poll(kPollMs) == MDR_RESULT_ERROR_NET)
                {
                    conn.Disconnect();
                    app.stage = Stage::Failed;
                    app.status = conn.LastError();
                    break;
                }

                int event = device.PollEvents();
                switch (event)
                {
                case MDR_HEADPHONES_TASK_INIT_OK:
                    device.Invoke(device.RequestSyncV2());
                    app.lastSync = std::chrono::steady_clock::now();
                    break;
                case MDR_HEADPHONES_IDLE:
                    if (device.IsDirty())
                    {
                        device.Invoke(device.RequestCommitV2());
                    }
                    else if (std::chrono::steady_clock::now() - app.lastSync >= kSyncInterval)
                    {
                        device.Invoke(device.RequestSyncV2());
                        app.lastSync = std::chrono::steady_clock::now();
                    }
                    break;
                case MDR_HEADPHONES_ERROR:
                    conn.Disconnect();
                    app.stage = Stage::Failed;
                    app.status = std::string("Device error: ") + device.GetLastError();
                    break;
                default:
                    break;
                }
                break;
            }

            case Stage::Failed:
                break;
            }
        }
        catch (const std::exception& e)
        {
            app.stage = Stage::Failed;
            app.status = e.what();
        }
    }
}

int main()
{
    tui::Connection conn;
    if (!conn.valid())
    {
        fputs("Failed to initialize Bluetooth backend.\n", stderr);
        return 1;
    }

    App app;
    mdr::MDRHeadphones device;

    auto rescan = [&]
    {
        app.deviceNames.clear();
        app.deviceMacs.clear();
        for (const auto& d : conn.ListDevices())
        {
            app.deviceNames.push_back(d.name.empty() ? d.mac : d.name);
            app.deviceMacs.push_back(d.mac);
        }
        if (app.selected >= static_cast<int>(app.deviceNames.size()))
            app.selected = 0;
    };
    rescan();

    auto screen = ScreenInteractive::Fullscreen();

    auto menu = Radiobox(&app.deviceNames, &app.selected);
    auto connectBtn = Button("Connect", [&]
    {
        if (app.stage == Stage::Picking && app.selected >= 0 &&
            app.selected < static_cast<int>(app.deviceMacs.size()))
        {
            app.requestedMac = app.deviceMacs[app.selected];
            app.connectRequested = true;
        }
    });
    auto rescanBtn = Button("Rescan", [&]
    {
        if (app.stage == Stage::Picking)
            rescan();
    });
    auto quitBtn = Button("Quit", [&] { screen.Exit(); });

    auto buttons = Container::Horizontal({connectBtn, rescanBtn, quitBtn});
    auto layout = Container::Vertical({menu, buttons});

    auto renderer = Renderer(layout, [&]
    {
        Element body;
        switch (app.stage)
        {
        case Stage::Connected:
            if (!device.IsReady())
                body = vbox({text("Connected ✓ — querying device...") | bold | color(Color::Green)});
            else
                body = tui::RenderDashboard(device);
            break;
        case Stage::Connecting:
            body = vbox({text("Connecting...") | bold, text(app.status) | dim});
            break;
        case Stage::Failed:
            body = vbox({
                text("Connection failed") | bold | color(Color::Red),
                text(app.status) | dim,
                separator(),
                text("Pick a device and Connect to retry:"),
                menu->Render(),
                hbox({connectBtn->Render(), rescanBtn->Render(), quitBtn->Render()}),
            });
            break;
        case Stage::Picking:
        default:
            Element list = app.deviceNames.empty()
                ? text("No devices. Pair a compatible Sony device, then Rescan.") | dim
                : menu->Render();
            body = vbox({
                text("Available devices") | bold,
                separator(),
                list,
                separator(),
                hbox({connectBtn->Render(), rescanBtn->Render(), quitBtn->Render()}),
            });
            break;
        }

        return vbox({
                   text("SonyHeadphonesClient — TUI " CLIENT_TUI_VERSION) | bold | center,
                   separator(),
                   body,
               }) |
               border;
    });

    auto root = CatchEvent(renderer, [&](const Event& e)
    {
        if (e == Event::Character('q') || e == Event::Escape)
        {
            screen.Exit();
            return true;
        }
        return false;
    });

    // Interleave libmdr (main thread) with FTXUI rendering.
    Loop loop(&screen, root);
    while (!loop.HasQuitted())
    {
        loop.RunOnce();              // handle queued input + render
        PumpStep(conn, device, app); // drive libmdr; conn.Poll() services the run loop
        // Connecting/Connected: PumpStep's Poll(kPollMs) paces the loop; keep
        // redrawing for live values. Idle stages: sleep so we don't busy-spin.
        if (app.stage == Stage::Connecting || app.stage == Stage::Connected)
            screen.PostEvent(Event::Custom);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return 0;
}
