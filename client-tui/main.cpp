// SonyHeadphonesClient — btop-style terminal client (Phase 1: scaffold + connect)
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <mdr/Headphones.hpp>
#include "Connection.hpp"

using namespace ftxui;
using namespace std::chrono_literals;

namespace
{
    enum class Stage
    {
        Picking,
        Connecting,
        Connected,
        Failed
    };

    // Shared state between the UI thread and the pump thread.
    struct Model
    {
        std::mutex m;
        Stage stage = Stage::Picking;
        std::string status;
        std::string modelName;
        std::string fwVersion;

        std::string requestedMac;
        std::atomic<bool> connectRequested{false};
        std::atomic<bool> quit{false};
    };

    // Owns the connection and (once connected) the MDRHeadphones device.
    // This is the ONLY thread that touches libmdr after a connect is requested.
    void PumpThread(tui::Connection& conn, Model& model, ScreenInteractive& screen)
    {
        mdr::MDRHeadphones device;

        auto setStage = [&](Stage s, std::string status)
        {
            std::lock_guard lk(model.m);
            model.stage = s;
            model.status = std::move(status);
        };

        while (!model.quit.load())
        {
            Stage stage;
            {
                std::lock_guard lk(model.m);
                stage = model.stage;
            }

            try
            {
                switch (stage)
                {
                case Stage::Picking:
                {
                    if (model.connectRequested.exchange(false))
                    {
                        std::string mac;
                        {
                            std::lock_guard lk(model.m);
                            mac = model.requestedMac;
                        }
                        setStage(Stage::Connecting, "Connecting...");
                        int res = conn.Connect(mac, MDR_SERVICE_UUID_XM5);
                        if (res != MDR_RESULT_OK && res != MDR_RESULT_INPROGRESS)
                            setStage(Stage::Failed, conn.LastError());
                        screen.PostEvent(Event::Custom);
                    }
                    else
                    {
                        std::this_thread::sleep_for(50ms);
                    }
                    break;
                }

                case Stage::Connecting:
                {
                    int res = conn.Poll(0);
                    if (res == MDR_RESULT_OK)
                    {
                        device = mdr::MDRHeadphones(conn.raw());
                        device.Invoke(device.RequestInitV2());
                        setStage(Stage::Connected, "Connected");
                    }
                    else if (res != MDR_RESULT_INPROGRESS && res != MDR_RESULT_ERROR_TIMEOUT)
                    {
                        conn.Disconnect();
                        setStage(Stage::Failed, conn.LastError());
                    }
                    screen.PostEvent(Event::Custom);
                    std::this_thread::sleep_for(30ms);
                    break;
                }

                case Stage::Connected:
                {
                    int event = device.PollEvents();
                    switch (event)
                    {
                    case MDR_HEADPHONES_TASK_INIT_OK:
                        // Pull values that the device won't push on its own (battery, etc.)
                        device.Invoke(device.RequestSyncV2());
                        break;
                    case MDR_HEADPHONES_IDLE:
                        if (device.IsDirty())
                            device.Invoke(device.RequestCommitV2());
                        break;
                    case MDR_HEADPHONES_ERROR:
                        conn.Disconnect();
                        setStage(Stage::Failed, std::string("Device error: ") + device.GetLastError());
                        break;
                    default:
                        break;
                    }

                    {
                        std::lock_guard lk(model.m);
                        model.modelName = device.mModelName;
                        model.fwVersion = device.mFWVersion;
                    }
                    screen.PostEvent(Event::Custom);
                    std::this_thread::sleep_for(30ms);
                    break;
                }

                case Stage::Failed:
                    std::this_thread::sleep_for(100ms);
                    break;
                }
            }
            catch (const std::exception& e)
            {
                setStage(Stage::Failed, e.what());
                screen.PostEvent(Event::Custom);
            }
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

    // Initial device scan (main thread; pump thread owns the connection afterwards).
    std::vector<std::string> deviceNames;
    std::vector<std::string> deviceMacs;
    auto rescan = [&]
    {
        deviceNames.clear();
        deviceMacs.clear();
        for (const auto& d : conn.ListDevices())
        {
            deviceNames.push_back(d.name.empty() ? d.mac : d.name);
            deviceMacs.push_back(d.mac);
        }
    };
    rescan();

    Model model;
    auto screen = ScreenInteractive::Fullscreen();

    int selected = 0;
    auto menu = Radiobox(&deviceNames, &selected);

    auto connectBtn = Button("Connect", [&]
    {
        std::lock_guard lk(model.m);
        if (model.stage == Stage::Picking && selected >= 0 &&
            selected < static_cast<int>(deviceMacs.size()))
        {
            model.requestedMac = deviceMacs[selected];
            model.connectRequested = true;
        }
    });
    auto rescanBtn = Button("Rescan", [&]
    {
        std::lock_guard lk(model.m);
        if (model.stage == Stage::Picking)
            rescan();
    });
    auto quitBtn = Button("Quit", [&]
    {
        model.quit = true;
        screen.Exit();
    });

    auto buttons = Container::Horizontal({connectBtn, rescanBtn, quitBtn});
    auto layout = Container::Vertical({menu, buttons});

    auto renderer = Renderer(layout, [&]
    {
        Stage stage;
        std::string status, modelName, fw;
        {
            std::lock_guard lk(model.m);
            stage = model.stage;
            status = model.status;
            modelName = model.modelName;
            fw = model.fwVersion;
        }

        Element body;
        switch (stage)
        {
        case Stage::Connected:
            body = vbox({
                text("Connected ✓") | bold | color(Color::Green),
                separator(),
                text("Model:    " + (modelName.empty() ? std::string("(querying...)") : modelName)),
                text("Firmware: " + (fw.empty() ? std::string("(querying...)") : fw)),
                text(""),
                text("Phase 1 OK — protocol engine live. Press q to quit.") | dim,
            });
            break;
        case Stage::Connecting:
            body = vbox({text("Connecting...") | bold, text(status) | dim});
            break;
        case Stage::Failed:
            body = vbox({
                text("Connection failed") | bold | color(Color::Red),
                text(status) | dim,
                separator(),
                text("Pick a device and Connect to retry:"),
                menu->Render(),
                hbox({connectBtn->Render(), rescanBtn->Render(), quitBtn->Render()}),
            });
            break;
        case Stage::Picking:
        default:
            Element list = deviceNames.empty()
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
            model.quit = true;
            screen.Exit();
            return true;
        }
        return false;
    });

    std::thread pump(PumpThread, std::ref(conn), std::ref(model), std::ref(screen));
    screen.Loop(root);

    model.quit = true;
    pump.join();
    return 0;
}
