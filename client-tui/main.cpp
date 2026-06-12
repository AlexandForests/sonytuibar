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
#include "Controls.hpp"
#ifdef TUI_HAS_AUDIO_TAP
#include "AudioTap_macOS.hpp"
#endif

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

        bool initialized = false; // true once the first InitV2 completes (support tables ready)
    };

    // Everything the dashboard displays, flattened for cheap comparison. We only
    // post a redraw when this changes (or on a slow heartbeat) — re-rendering
    // every pump iteration burned CPU around the clock.
    struct ViewState
    {
        int batL = -1, batR = -1, batCase = -1;
        int chgL = -1, chgR = -1, chgCase = -1;
        bool thrL = false, thrR = false, thrCase = false;
        bool ncEnabled = false;
        int ncMode = -1, ambLevel = -1;
        bool voice = false;
        int volume = -1, eqPreset = -1, codec = -1, playPause = -1;
        bool eqAvail = false, upscaling = false;
        std::vector<int> eqBands;
        std::string title, artist, model, fw;

        bool operator==(const ViewState&) const = default;
    };

    ViewState Snapshot(const mdr::MDRHeadphones& d)
    {
        ViewState s;
        s.batL = d.mBatteryL.level;
        s.batR = d.mBatteryR.level;
        s.batCase = d.mBatteryCase.level;
        s.chgL = static_cast<int>(d.mBatteryL.charging);
        s.chgR = static_cast<int>(d.mBatteryR.charging);
        s.chgCase = static_cast<int>(d.mBatteryCase.charging);
        s.thrL = d.mBatteryL.threshold;
        s.thrR = d.mBatteryR.threshold;
        s.thrCase = d.mBatteryCase.threshold;
        s.ncEnabled = d.mNcAsmEnabled.current;
        s.ncMode = static_cast<int>(d.mNcAsmMode.current);
        s.ambLevel = d.mNcAsmAmbientLevel.current;
        s.voice = d.mNcAsmFocusOnVoice.current;
        s.volume = d.mPlayVolume.current;
        s.eqPreset = static_cast<int>(d.mEqPresetId.current);
        s.codec = static_cast<int>(d.mAudioCodec);
        s.playPause = static_cast<int>(d.mPlayPause);
        s.eqAvail = d.mEqAvailable.current;
        s.upscaling = d.mUpscalingEnabled.current;
        s.eqBands = d.mEqConfig.current;
        s.title = d.mPlayTrackTitle;
        s.artist = d.mPlayTrackArtist;
        s.model = d.mModelName;
        s.fw = d.mFWVersion;
        return s;
    }

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
                    app.initialized = false;
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
                    app.initialized = true;
                    device.Invoke(device.RequestSyncV2()); // one sync in clean seq state
                    break;
                case MDR_HEADPHONES_IDLE:
                    // No periodic poll: battery refreshes via device push
                    // notifications (PollEvents folds them into .current). Polling
                    // POWER_GET_STATUS races those notifications and desyncs the
                    // protocol seq, hanging the link. We only commit dirty props.
                    if (device.IsDirty())
                        device.Invoke(device.RequestCommitV2());
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

#ifdef TUI_HAS_AUDIO_TAP
    tui::AudioTap tap;            // system-audio visualizer, off by default
    tui::SpectrumAnalyzer analyzer;
    std::vector<float> vizBands;
#endif

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
        // Connected: full-bleed btop-style dashboard, no outer chrome.
        if (app.stage == Stage::Connected && app.initialized)
        {
            const std::vector<float>* spectrum = nullptr;
            Element footer = tui::Footer(device);
#ifdef TUI_HAS_AUDIO_TAP
            if (tap.Active())
                spectrum = &vizBands;
            footer = hbox({footer, text("v") | bold | color(Color::Cyan),
                           text(":viz  ") | dim});
#endif
            return vbox({
                tui::RenderDashboard(device, spectrum),
                filler(),
                separator(),
                footer,
            });
        }

        Element body;
        switch (app.stage)
        {
        case Stage::Connected:
            body = vbox({text("Connected ✓ — querying device...") | bold | color(Color::Green)});
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
        if (app.stage == Stage::Connected)
        {
#ifdef TUI_HAS_AUDIO_TAP
            if (e == Event::Character('v'))
            {
                // First-ever Start() triggers the System Audio Recording prompt.
                if (tap.Active())
                    tap.Stop();
                else
                    tap.Start();
                return true;
            }
#endif
            if (tui::HandleControl(e, device))
                return true;
            // Esc cancels a pending confirm rather than quitting.
            if (e == Event::Character('q') || e == Event::Escape)
            {
                screen.Exit();
                return true;
            }
            // The device picker is hidden — don't let its widgets consume
            // navigation/clicks behind the dashboard.
            return true;
        }

        if (e == Event::Character('q') || e == Event::Escape)
        {
            screen.Exit();
            return true;
        }
        return false;
    });

    // Interleave libmdr (main thread) with FTXUI rendering.
    Loop loop(&screen, root);
    ViewState lastView;
    auto lastDraw = std::chrono::steady_clock::now();
    while (!loop.HasQuitted())
    {
        loop.RunOnce();              // handle queued input + render
        PumpStep(conn, device, app); // drive libmdr; conn.Poll() services the run loop
        // Connecting/Connected: PumpStep's Poll(kPollMs) paces the loop.
        // Idle stages: sleep so we don't busy-spin.
        if (app.stage == Stage::Connecting)
        {
            screen.PostEvent(Event::Custom); // animate status until connected
        }
        else if (app.stage == Stage::Connected)
        {
            // Redraw only when displayed state changed (push notification,
            // committed control, init progress) or on a 500ms heartbeat.
            // Keypresses trigger FTXUI redraws on their own.
            auto heartbeat = std::chrono::milliseconds(500);
#ifdef TUI_HAS_AUDIO_TAP
            if (tap.Active())
            {
                vizBands = analyzer.Compute(tap);
                heartbeat = std::chrono::milliseconds(33); // ~30fps while visualizing
            }
#endif
            auto now = std::chrono::steady_clock::now();
            ViewState view = Snapshot(device);
            if (view != lastView || now - lastDraw >= heartbeat)
            {
                lastView = std::move(view);
                lastDraw = now;
                screen.PostEvent(Event::Custom);
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    return 0;
}
