#pragma once

#include <functional>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

// Looks on GitHub for a newer Egret EQ, once each time the plugin is started
// (shared by every instance in the host), and installs it on request: on
// Windows it downloads the installer and starts it, elsewhere it opens the
// release page. The only thing it sends is an ordinary request for the
// latest release's public details.
class UpdateChecker : private juce::Thread
{
public:
    struct Result
    {
        enum class State { idle, checking, upToDate, newer, failed };
        State state = State::idle;
        juce::String latest, pageUrl, installerUrl;
    };

    UpdateChecker();
    ~UpdateChecker() override;

    void checkOnLaunch();   // once per run of the host, when automatic checks are on
    void checkNow();
    Result getResult() const;

    bool isAutomatic() const;
    void setAutomatic (bool);
    void dismiss (const juce::String& version) { const juce::ScopedLock sl (lock); dismissed = version; }
    bool isDismissed (const juce::String& version) const { const juce::ScopedLock sl (lock); return dismissed == version; }

    // Windows: download the installer (progress 0..1 on the message thread), then start it.
    // Elsewhere: open the release page.
    void install (std::function<void (float progress, const juce::String& status)> report);

    static juce::String currentVersion();
    static bool isNewer (const juce::String& latest, const juce::String& current);
    static constexpr const char* repo = "vox4vulpes/EgretFX-EQ";

private:
    void run() override;
    std::unique_ptr<juce::PropertiesFile> settings();

    juce::CriticalSection lock;
    Result result;
    juce::String dismissed;
    std::unique_ptr<juce::Thread> downloader;
};
