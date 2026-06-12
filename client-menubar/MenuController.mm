#import "MenuController.h"

#import <ServiceManagement/ServiceManagement.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>

#include <mdr/Headphones.hpp>
#include <mdr/ProtocolV2T1.hpp>
#include "Connection.hpp"

namespace v2t1 = mdr::v2::t1;
using F1 = mdr::v2::MessageMdrV2FunctionType_Table1;

namespace
{
    enum class Stage
    {
        Disconnected,
        Connecting,
        Connected
    };

    constexpr double kPumpInterval = 0.05;  // 50ms pump tick
    constexpr int kUiRefreshTicks = 5;      // refresh menu state every 250ms
    constexpr double kRetryInterval = 15.0; // auto-reconnect cadence
    constexpr double kConnectTimeout = 30.0;

    NSString* const kDefaultsMacKey = @"lastDeviceMac";
    NSString* const kDefaultsNameKey = @"lastDeviceName";

    // --- Support checks + formatters, duplicated from client-tui (Controls.cpp/
    // Panels.cpp are FTXUI-coupled TUs; the relevant logic is these few lines). ---

    bool Has(const mdr::MDRHeadphones& d, F1 f) { return d.mSupport.contains(f); }

    bool SupportsNc(const mdr::MDRHeadphones& d)
    {
        return Has(d, F1::NOISE_CANCELLING_ONOFF) ||
               Has(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_ONOFF) ||
               Has(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::NOISE_CANCELLING_DUAL_SINGLE_OFF_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_SINGLE_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT_NOISE_ADAPTATION);
    }

    bool SupportsAsm(const mdr::MDRHeadphones& d)
    {
        return Has(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_ONOFF) ||
               Has(d, F1::NOISE_CANCELLING_ONOFF_AND_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::NOISE_CANCELLING_DUAL_SINGLE_OFF_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::AMBIENT_SOUND_MODE_ONOFF) ||
               Has(d, F1::AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::AMBIENT_SOUND_CONTROL_MODE_SELECT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_SINGLE_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT) ||
               Has(d, F1::MODE_NC_ASM_NOISE_CANCELLING_DUAL_AMBIENT_SOUND_MODE_LEVEL_ADJUSTMENT_NOISE_ADAPTATION);
    }

    const char* CodecName(v2t1::AudioCodec c)
    {
        using enum v2t1::AudioCodec;
        switch (c)
        {
        case SBC: return "SBC";
        case AAC: return "AAC";
        case LDAC: return "LDAC";
        case APT_X: return "aptX";
        case APT_X_HD: return "aptX HD";
        case LC3: return "LC3";
        default: return nullptr;
        }
    }

    // Only the presets the WH-1000XM5 actually applies (see client-tui notes).
    struct EqEntry
    {
        v2t1::EqPresetId id;
        const char* name;
    };
    constexpr std::array<EqEntry, 9> kEqPresets = {{
        {v2t1::EqPresetId::OFF, "Off"},
        {v2t1::EqPresetId::BRIGHT, "Bright"},
        {v2t1::EqPresetId::EXCITED, "Excited"},
        {v2t1::EqPresetId::MELLOW, "Mellow"},
        {v2t1::EqPresetId::RELAXED, "Relaxed"},
        {v2t1::EqPresetId::VOCAL, "Vocal"},
        {v2t1::EqPresetId::TREBLE, "Treble"},
        {v2t1::EqPresetId::BASS, "Bass"},
        {v2t1::EqPresetId::SPEECH, "Speech"},
    }};

    NSString* BatteryText(const char* label, const mdr::MDRHeadphones::BatteryState& b)
    {
        NSString* s = [NSString stringWithFormat:@"%s %d%%", label, b.level];
        if (b.charging == v2t1::BatteryChargingStatus::CHARGING)
            s = [s stringByAppendingString:@" ⚡"];
        return s;
    }
}

@implementation MenuController
{
    NSStatusItem* _statusItem;
    NSMenu* _menu;
    NSTimer* _timer;
    int _tick;

    std::unique_ptr<tui::Connection> _conn;
    mdr::MDRHeadphones _device;
    Stage _stage;
    bool _initialized;

    NSString* _targetMac;  // device we want to be connected to (nil = none)
    NSString* _targetName;
    NSDate* _nextRetry;
    NSDate* _connectDeadline;
    NSString* _statusText;

    // Live-updated items
    NSMenuItem* _headerItem;
    NSMenuItem* _statusLine;
    NSMenuItem* _batteryItem;
    NSMenuItem* _batteryCaseItem;
    NSMenuItem* _noiseOffItem;
    NSMenuItem* _noiseNcItem;
    NSMenuItem* _noiseAsmItem;
    NSMenuItem* _ambientRow;
    NSSlider* _ambientSlider;
    NSTextField* _ambientValue;
    NSMenuItem* _eqRoot;
    NSMenu* _eqMenu;
    NSMenuItem* _volumeRow;
    NSSlider* _volumeSlider;
    NSTextField* _volumeValue;
    NSMenu* _deviceMenu;
    NSMenuItem* _disconnectItem;
    NSMenuItem* _launchItem;

    NSString* _lastBarTitle;
}

- (instancetype)init
{
    self = [super init];
    if (!self)
        return nil;

    _conn = std::make_unique<tui::Connection>();
    _stage = Stage::Disconnected;
    _initialized = false;
    _statusText = @"Not connected";
    _nextRetry = [NSDate distantPast];

    [self buildStatusItem];
    [self buildMenu];

    // Auto-connect to the remembered device.
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    _targetMac = [defaults stringForKey:kDefaultsMacKey];
    _targetName = [defaults stringForKey:kDefaultsNameKey];

    // Common modes is load-bearing: default-mode timers pause while the menu
    // is open — exactly when the user is changing settings.
    _timer = [NSTimer timerWithTimeInterval:kPumpInterval
                                     target:self
                                   selector:@selector(pump)
                                   userInfo:nil
                                    repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];

    [self refreshUI];
    return self;
}

- (void)shutdown
{
    [_timer invalidate];
    _timer = nil;
    if (_conn)
        _conn->Disconnect();
}

// --- UI construction ---

- (void)buildStatusItem
{
    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    NSImage* img = [NSImage imageWithSystemSymbolName:@"headphones"
                             accessibilityDescription:@"Sony Headphones"];
    _statusItem.button.image = img;
    _statusItem.button.imagePosition = NSImageLeft;
}

- (NSMenuItem*)sliderRowWithLabel:(NSString*)label
                              min:(double)min
                              max:(double)max
                           action:(SEL)action
                        outSlider:(NSSlider* __strong*)outSlider
                         outValue:(NSTextField* __strong*)outValue
{
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 280, 26)];

    NSTextField* name = [NSTextField labelWithString:label];
    name.font = [NSFont menuFontOfSize:13];
    name.textColor = [NSColor secondaryLabelColor];
    name.frame = NSMakeRect(14, 5, 62, 17);

    NSSlider* slider = [[NSSlider alloc] initWithFrame:NSMakeRect(78, 3, 156, 20)];
    slider.minValue = min;
    slider.maxValue = max;
    slider.continuous = YES;
    slider.target = self;
    slider.action = action;

    NSTextField* value = [NSTextField labelWithString:@"–"];
    value.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular];
    value.alignment = NSTextAlignmentRight;
    value.frame = NSMakeRect(238, 5, 34, 17);

    [view addSubview:name];
    [view addSubview:slider];
    [view addSubview:value];

    NSMenuItem* item = [[NSMenuItem alloc] init];
    item.view = view;
    *outSlider = slider;
    *outValue = value;
    return item;
}

- (void)buildMenu
{
    _menu = [[NSMenu alloc] init];
    _menu.delegate = self;
    _menu.autoenablesItems = NO;

    auto disabledItem = [](NSString* title)
    {
        NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        it.enabled = NO;
        return it;
    };

    _headerItem = disabledItem(@"Sony Headphones");
    [_menu addItem:_headerItem];
    _statusLine = disabledItem(@"Not connected");
    [_menu addItem:_statusLine];
    _batteryItem = disabledItem(@"Battery –");
    [_menu addItem:_batteryItem];
    _batteryCaseItem = disabledItem(@"Case –");
    _batteryCaseItem.hidden = YES;
    [_menu addItem:_batteryCaseItem];

    [_menu addItem:[NSMenuItem separatorItem]];

    _noiseOffItem = [[NSMenuItem alloc] initWithTitle:@"Off"
                                               action:@selector(noiseOff:)
                                        keyEquivalent:@""];
    _noiseOffItem.target = self;
    [_menu addItem:_noiseOffItem];

    _noiseNcItem = [[NSMenuItem alloc] initWithTitle:@"Noise Cancelling"
                                              action:@selector(noiseNc:)
                                       keyEquivalent:@""];
    _noiseNcItem.target = self;
    [_menu addItem:_noiseNcItem];

    _noiseAsmItem = [[NSMenuItem alloc] initWithTitle:@"Ambient"
                                               action:@selector(noiseAsm:)
                                        keyEquivalent:@""];
    _noiseAsmItem.target = self;
    [_menu addItem:_noiseAsmItem];

    _ambientRow = [self sliderRowWithLabel:@"Ambient"
                                       min:1
                                       max:20
                                    action:@selector(ambientChanged:)
                                 outSlider:&_ambientSlider
                                  outValue:&_ambientValue];
    [_menu addItem:_ambientRow];

    [_menu addItem:[NSMenuItem separatorItem]];

    _eqRoot = [[NSMenuItem alloc] initWithTitle:@"Equalizer" action:nil keyEquivalent:@""];
    _eqMenu = [[NSMenu alloc] init];
    _eqMenu.autoenablesItems = NO;
    for (const auto& preset : kEqPresets)
    {
        NSMenuItem* it = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:preset.name]
                   action:@selector(eqSelected:)
            keyEquivalent:@""];
        it.target = self;
        it.representedObject = @(static_cast<int>(preset.id));
        [_eqMenu addItem:it];
    }
    _eqRoot.submenu = _eqMenu;
    [_menu addItem:_eqRoot];

    _volumeRow = [self sliderRowWithLabel:@"Volume"
                                      min:0
                                      max:30
                                   action:@selector(volumeChanged:)
                                outSlider:&_volumeSlider
                                 outValue:&_volumeValue];
    [_menu addItem:_volumeRow];

    [_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* deviceRoot = [[NSMenuItem alloc] initWithTitle:@"Device"
                                                        action:nil
                                                 keyEquivalent:@""];
    _deviceMenu = [[NSMenu alloc] init];
    _deviceMenu.delegate = self; // rebuilt on open
    _deviceMenu.autoenablesItems = NO;
    deviceRoot.submenu = _deviceMenu;
    [_menu addItem:deviceRoot];

    _launchItem = [[NSMenuItem alloc] initWithTitle:@"Launch at Login"
                                             action:@selector(toggleLaunchAtLogin:)
                                      keyEquivalent:@""];
    _launchItem.target = self;
    [_menu addItem:_launchItem];

    NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                  action:@selector(quit:)
                                           keyEquivalent:@"q"];
    quit.target = self;
    [_menu addItem:quit];

    _statusItem.menu = _menu;
}

// --- libmdr pump (mirror of client-tui PumpStep; same hard-won rules:
// single thread, sync once after init, never poll battery, commit iff dirty) ---

- (void)startConnect
{
    if (!_targetMac || !_conn->valid())
        return;
    _initialized = false;
    int res = _conn->Connect([_targetMac UTF8String], MDR_SERVICE_UUID_XM5);
    if (res == MDR_RESULT_OK || res == MDR_RESULT_INPROGRESS)
    {
        _stage = Stage::Connecting;
        _statusText = @"Connecting…";
        _connectDeadline = [NSDate dateWithTimeIntervalSinceNow:kConnectTimeout];
    }
    else
    {
        _statusText = [NSString stringWithUTF8String:_conn->LastError()];
        _nextRetry = [NSDate dateWithTimeIntervalSinceNow:kRetryInterval];
    }
}

- (void)dropWithStatus:(NSString*)status retry:(BOOL)retry
{
    _conn->Disconnect();
    _stage = Stage::Disconnected;
    _initialized = false;
    _statusText = status;
    _nextRetry = (retry && _targetMac)
                     ? [NSDate dateWithTimeIntervalSinceNow:kRetryInterval]
                     : [NSDate distantFuture];
}

- (void)pumpStep
{
    try
    {
        switch (_stage)
        {
        case Stage::Disconnected:
            if (_targetMac && [_nextRetry timeIntervalSinceNow] <= 0)
            {
                _nextRetry = [NSDate dateWithTimeIntervalSinceNow:kRetryInterval];
                [self startConnect];
            }
            break;

        case Stage::Connecting:
        {
            int res = _conn->Poll(1);
            if (res == MDR_RESULT_OK)
            {
                _device = mdr::MDRHeadphones(_conn->raw());
                _device.Invoke(_device.RequestInitV2());
                _stage = Stage::Connected;
                _statusText = @"Connected";
            }
            else if (res != MDR_RESULT_INPROGRESS && res != MDR_RESULT_ERROR_TIMEOUT)
            {
                [self dropWithStatus:[NSString stringWithUTF8String:_conn->LastError()]
                               retry:YES];
            }
            else if ([_connectDeadline timeIntervalSinceNow] <= 0)
            {
                [self dropWithStatus:@"Connect timed out" retry:YES];
            }
            break;
        }

        case Stage::Connected:
        {
            // Brief nested run-loop turn; also keeps IOBluetooth callbacks
            // flowing while the menu holds the loop in event-tracking mode.
            if (_conn->Poll(1) == MDR_RESULT_ERROR_NET)
            {
                [self dropWithStatus:[NSString stringWithUTF8String:_conn->LastError()]
                               retry:YES];
                break;
            }

            int event = _device.PollEvents();
            switch (event)
            {
            case MDR_HEADPHONES_TASK_INIT_OK:
                _initialized = true;
                _device.Invoke(_device.RequestSyncV2()); // one sync in clean seq state
                break;
            case MDR_HEADPHONES_IDLE:
                // No periodic poll — battery updates arrive as device pushes.
                if (_device.IsDirty())
                    _device.Invoke(_device.RequestCommitV2());
                break;
            case MDR_HEADPHONES_ERROR:
                [self dropWithStatus:[NSString stringWithFormat:@"Device error: %s",
                                                                _device.GetLastError()]
                               retry:YES];
                break;
            default:
                break;
            }
            break;
        }
        }
    }
    catch (const std::exception& e)
    {
        [self dropWithStatus:[NSString stringWithUTF8String:e.what()] retry:YES];
    }
}

- (void)pump
{
    [self pumpStep];
    if (++_tick % kUiRefreshTicks == 0)
        [self refreshUI];
}

// --- UI refresh ---

- (void)refreshUI
{
    bool connected = _stage == Stage::Connected && _initialized;

    // Status item: icon + live battery %.
    NSString* barTitle = @"";
    if (connected)
    {
        if (_device.mBatteryL.threshold)
            barTitle = [NSString stringWithFormat:@" %d%%", _device.mBatteryL.level];
    }
    else if (_stage == Stage::Connecting)
    {
        barTitle = @" …";
    }
    if (![barTitle isEqualToString:_lastBarTitle ?: @""])
    {
        _statusItem.button.title = barTitle;
        _lastBarTitle = barTitle;
    }
    _statusItem.button.appearsDisabled = !connected;

    // Header + status lines.
    if (connected)
    {
        std::string model = _device.mModelName.empty() ? "Sony Headphones" : _device.mModelName;
        NSMutableString* header = [NSMutableString stringWithUTF8String:model.c_str()];
        if (!_device.mFWVersion.empty())
            [header appendFormat:@"  fw %s", _device.mFWVersion.c_str()];
        if (Has(_device, F1::CODEC_INDICATOR))
            if (const char* codec = CodecName(_device.mAudioCodec))
                [header appendFormat:@"  ·  %s", codec];
        _headerItem.title = header;
        _statusLine.hidden = YES;
    }
    else
    {
        _headerItem.title = _targetName.length ? _targetName : @"Sony Headphones";
        _statusLine.hidden = NO;
        _statusLine.title = _statusText ?: @"Not connected";
    }

    // Battery rows (gating mirrors client-tui BatteryPanel).
    bool lr = connected && (Has(_device, F1::LEFT_RIGHT_BATTERY_LEVEL_INDICATOR) ||
                            Has(_device, F1::LR_BATTERY_LEVEL_WITH_THRESHOLD)) &&
              _device.mBatteryL.threshold && _device.mBatteryR.threshold;
    bool single = connected && !lr && _device.mBatteryL.threshold;
    bool casing = connected &&
                  (Has(_device, F1::CRADLE_BATTERY_LEVEL_INDICATOR) ||
                   Has(_device, F1::CRADLE_BATTERY_LEVEL_WITH_THRESHOLD)) &&
                  _device.mBatteryCase.threshold;

    _batteryItem.hidden = !(lr || single);
    if (lr)
        _batteryItem.title = [NSString stringWithFormat:@"%@   %@",
                                                        BatteryText("L", _device.mBatteryL),
                                                        BatteryText("R", _device.mBatteryR)];
    else if (single)
        _batteryItem.title = BatteryText("Battery", _device.mBatteryL);
    _batteryCaseItem.hidden = !casing;
    if (casing)
        _batteryCaseItem.title = BatteryText("Case", _device.mBatteryCase);

    // Noise controls (checkmarks track .desired so clicks feel instant).
    bool nc = connected && SupportsNc(_device);
    bool asm_ = connected && SupportsAsm(_device);
    bool noiseOff = !_device.mNcAsmEnabled.desired;
    bool noiseNc = !noiseOff && _device.mNcAsmMode.desired == v2t1::NcAsmMode::NC;

    _noiseOffItem.hidden = !(nc || asm_);
    _noiseNcItem.hidden = !nc;
    _noiseAsmItem.hidden = !asm_;
    _noiseOffItem.state = (connected && noiseOff) ? NSControlStateValueOn : NSControlStateValueOff;
    _noiseNcItem.state = (connected && noiseNc) ? NSControlStateValueOn : NSControlStateValueOff;
    _noiseAsmItem.state =
        (connected && !noiseOff && !noiseNc) ? NSControlStateValueOn : NSControlStateValueOff;

    _ambientRow.hidden = !asm_;
    if (asm_)
    {
        bool ambientActive = !noiseOff && !noiseNc;
        _ambientSlider.enabled = ambientActive;
        int lvl = std::clamp(_device.mNcAsmAmbientLevel.desired, 1, 20);
        if (!_ambientSlider.cell.isHighlighted) // don't fight an active drag
            _ambientSlider.intValue = lvl;
        _ambientValue.stringValue = [NSString stringWithFormat:@"%d", lvl];
        _ambientValue.textColor =
            ambientActive ? [NSColor labelColor] : [NSColor tertiaryLabelColor];
    }

    // EQ submenu.
    bool eq = connected && _device.mEqAvailable.current;
    _eqRoot.hidden = !eq;
    if (eq)
    {
        int current = static_cast<int>(_device.mEqPresetId.desired);
        for (NSMenuItem* it in _eqMenu.itemArray)
            it.state = ([it.representedObject intValue] == current) ? NSControlStateValueOn
                                                                    : NSControlStateValueOff;
    }

    // Volume.
    _volumeRow.hidden = !connected;
    if (connected)
    {
        int vol = std::clamp(_device.mPlayVolume.desired, 0, 30);
        if (!_volumeSlider.cell.isHighlighted)
            _volumeSlider.intValue = vol;
        _volumeValue.stringValue = [NSString stringWithFormat:@"%d", vol];
    }

    // Disconnect availability + launch-at-login state.
    _disconnectItem.enabled = _stage != Stage::Disconnected;
    if (@available(macOS 13.0, *))
        _launchItem.state = ([SMAppService mainAppService].status == SMAppServiceStatusEnabled)
                                ? NSControlStateValueOn
                                : NSControlStateValueOff;
}

// --- NSMenuDelegate ---

- (void)menuNeedsUpdate:(NSMenu*)menu
{
    if (menu == _menu)
    {
        [self refreshUI];
        return;
    }
    if (menu != _deviceMenu)
        return;

    // Rebuild the paired-device list each open (cheap: no scanning).
    [_deviceMenu removeAllItems];
    for (const auto& dev : _conn->ListDevices())
    {
        NSString* name = [NSString stringWithUTF8String:
                                       dev.name.empty() ? dev.mac.c_str() : dev.name.c_str()];
        NSString* mac = [NSString stringWithUTF8String:dev.mac.c_str()];
        NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:name
                                                    action:@selector(deviceSelected:)
                                             keyEquivalent:@""];
        it.target = self;
        it.representedObject = @{@"name" : name, @"mac" : mac};
        if (_targetMac && [mac isEqualToString:_targetMac] && _stage != Stage::Disconnected)
            it.state = NSControlStateValueOn;
        [_deviceMenu addItem:it];
    }
    if (_deviceMenu.numberOfItems == 0)
    {
        NSMenuItem* none = [[NSMenuItem alloc] initWithTitle:@"No paired devices"
                                                      action:nil
                                               keyEquivalent:@""];
        none.enabled = NO;
        [_deviceMenu addItem:none];
    }
    [_deviceMenu addItem:[NSMenuItem separatorItem]];
    _disconnectItem = [[NSMenuItem alloc] initWithTitle:@"Disconnect"
                                                 action:@selector(disconnectSelected:)
                                          keyEquivalent:@""];
    _disconnectItem.target = self;
    _disconnectItem.enabled = _stage != Stage::Disconnected;
    [_deviceMenu addItem:_disconnectItem];
}

// --- Actions (set .desired; the pump commits dirty props) ---

- (void)noiseOff:(id)sender
{
    if (_stage != Stage::Connected)
        return;
    _device.mNcAsmEnabled.desired = false;
    [self refreshUI];
}

- (void)noiseNc:(id)sender
{
    if (_stage != Stage::Connected)
        return;
    _device.mNcAsmEnabled.desired = true;
    _device.mNcAsmMode.desired = v2t1::NcAsmMode::NC;
    [self refreshUI];
}

- (void)noiseAsm:(id)sender
{
    if (_stage != Stage::Connected)
        return;
    _device.mNcAsmEnabled.desired = true;
    _device.mNcAsmMode.desired = v2t1::NcAsmMode::ASM;
    if (_device.mNcAsmAmbientLevel.desired == 0)
        _device.mNcAsmAmbientLevel.desired = 20;
    [self refreshUI];
}

- (void)ambientChanged:(NSSlider*)sender
{
    if (_stage != Stage::Connected)
        return;
    _device.mNcAsmAmbientLevel.desired = sender.intValue;
    _ambientValue.stringValue = [NSString stringWithFormat:@"%d", sender.intValue];
}

- (void)volumeChanged:(NSSlider*)sender
{
    if (_stage != Stage::Connected)
        return;
    _device.mPlayVolume.desired = sender.intValue;
    _volumeValue.stringValue = [NSString stringWithFormat:@"%d", sender.intValue];
}

- (void)eqSelected:(NSMenuItem*)sender
{
    if (_stage != Stage::Connected)
        return;
    _device.mEqPresetId.desired =
        static_cast<v2t1::EqPresetId>([sender.representedObject intValue]);
    [self refreshUI];
}

- (void)deviceSelected:(NSMenuItem*)sender
{
    NSDictionary* info = sender.representedObject;
    _targetMac = info[@"mac"];
    _targetName = info[@"name"];
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:_targetMac forKey:kDefaultsMacKey];
    [defaults setObject:_targetName forKey:kDefaultsNameKey];

    if (_stage != Stage::Disconnected)
        [self dropWithStatus:@"Switching device…" retry:NO];
    [self startConnect];
    [self refreshUI];
}

- (void)disconnectSelected:(id)sender
{
    _targetMac = nil; // explicit disconnect: stop auto-reconnect this session
    _targetName = nil;
    [self dropWithStatus:@"Disconnected" retry:NO];
    [self refreshUI];
}

- (void)toggleLaunchAtLogin:(id)sender
{
    if (@available(macOS 13.0, *))
    {
        SMAppService* svc = [SMAppService mainAppService];
        NSError* err = nil;
        if (svc.status == SMAppServiceStatusEnabled)
            [svc unregisterAndReturnError:&err];
        else
            [svc registerAndReturnError:&err];
        if (err)
            _statusText = [NSString stringWithFormat:@"Login item: %@", err.localizedDescription];
    }
    [self refreshUI];
}

- (void)quit:(id)sender
{
    [NSApp terminate:nil];
}

@end
