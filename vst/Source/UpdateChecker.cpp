#include "UpdateChecker.h"

namespace
{
    std::atomic<bool> checkedThisRun { false };   // one look per run of the host, however many instances

    juce::URL::InputStreamOptions requestOptions (int timeoutMs)
    {
        return juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                   .withConnectionTimeoutMs (timeoutMs)
                   .withNumRedirectsToFollow (5)
                   .withExtraHeaders ("User-Agent: Egret-EQ/" + UpdateChecker::currentVersion() + "\r\nAccept: application/vnd.github+json");
    }

    juce::Array<int> versionParts (juce::String v)
    {
        v = v.trim().trimCharactersAtStart ("vV");
        juce::Array<int> parts;
        for (const auto& t : juce::StringArray::fromTokens (v, ".", {}))
            parts.add (t.getIntValue());
        while (parts.size() < 3) parts.add (0);
        return parts;
    }
}

UpdateChecker::UpdateChecker() : juce::Thread ("Egret EQ update check") {}

UpdateChecker::~UpdateChecker()
{
    stopThread (4000);
    if (downloader != nullptr) downloader->stopThread (4000);
}

juce::String UpdateChecker::currentVersion()
{
   #ifdef JucePlugin_VersionString
    return JucePlugin_VersionString;
   #else
    return "0.0.0";
   #endif
}

bool UpdateChecker::isNewer (const juce::String& latest, const juce::String& current)
{
    const auto a = versionParts (latest), b = versionParts (current);
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

std::unique_ptr<juce::PropertiesFile> UpdateChecker::settings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Egret EQ";
    o.folderName = "Egret";
    o.filenameSuffix = "settings";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}

bool UpdateChecker::isAutomatic() const
{
    return const_cast<UpdateChecker*> (this)->settings()->getBoolValue ("checkForUpdates", true);
}

void UpdateChecker::setAutomatic (bool on)
{
    auto s = settings();
    s->setValue ("checkForUpdates", on);
    s->saveIfNeeded();
}

void UpdateChecker::checkOnLaunch()
{
    if (checkedThisRun.exchange (true) || ! isAutomatic())
        return;
    checkNow();
}

void UpdateChecker::checkNow()
{
    if (isThreadRunning())
        return;
    {
        const juce::ScopedLock sl (lock);
        result.state = Result::State::checking;
    }
    startThread (juce::Thread::Priority::low);
}

UpdateChecker::Result UpdateChecker::getResult() const
{
    const juce::ScopedLock sl (lock);
    return result;
}

void UpdateChecker::run()
{
    Result r;
    r.state = Result::State::failed;
    const juce::URL api ("https://api.github.com/repos/" + juce::String (repo) + "/releases/latest");
    if (auto stream = api.createInputStream (requestOptions (8000)))
    {
        const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
        const auto tag = json.getProperty ("tag_name", {}).toString();
        if (tag.isNotEmpty())
        {
            r.latest = tag.trimCharactersAtStart ("vV");
            r.pageUrl = json.getProperty ("html_url", {}).toString();
            if (auto* assets = json.getProperty ("assets", {}).getArray())
                for (const auto& a : *assets)
                    if (a.getProperty ("name", {}).toString().endsWithIgnoreCase ("Setup-" + r.latest + ".exe"))
                        r.installerUrl = a.getProperty ("browser_download_url", {}).toString();
            r.state = isNewer (r.latest, currentVersion()) ? Result::State::newer : Result::State::upToDate;
        }
    }
    if (threadShouldExit())
        return;
    const juce::ScopedLock sl (lock);
    result = r;
}

void UpdateChecker::install (std::function<void (float, const juce::String&)> report)
{
    const auto r = getResult();
   #if JUCE_WINDOWS
    if (r.installerUrl.isNotEmpty())
    {
        if (downloader != nullptr && downloader->isThreadRunning())
            return;
        struct Download : juce::Thread
        {
            Download (juce::String u, std::function<void (float, const juce::String&)> rep, juce::String page)
                : juce::Thread ("Egret EQ download"), url (std::move (u)), report (std::move (rep)), pageUrl (std::move (page)) {}
            void tell (float p, const juce::String& s)
            {
                auto cb = report;
                juce::MessageManager::callAsync ([cb, p, s] { if (cb) cb (p, s); });
            }
            void run() override
            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                      .getChildFile (juce::URL (url).getFileName().isNotEmpty() ? juce::URL (url).getFileName() : juce::String ("Egret-EQ-Setup.exe"));
                file.deleteFile();
                int status = 0;
                auto stream = juce::URL (url).createInputStream (requestOptions (15000).withStatusCode (&status));
                if (stream == nullptr || status >= 400) { tell (-1, "Couldn't download; opening the release page"); openPage(); return; }
                {
                    juce::FileOutputStream out (file);
                    if (! out.openedOk()) { tell (-1, "Couldn't save the installer; opening the release page"); openPage(); return; }
                    const auto total = stream->getTotalLength();
                    juce::HeapBlock<char> buffer (65536);
                    juce::int64 done = 0;
                    while (! threadShouldExit())
                    {
                        const auto n = stream->read (buffer, 65536);
                        if (n <= 0) break;
                        out.write (buffer, (size_t) n);
                        done += n;
                        if (total > 0) tell ((float) done / (float) total, "Downloading...");
                    }
                    out.flush();
                }
                if (threadShouldExit()) return;
                tell (1.0f, "Starting the installer; close your DAW if it asks");
                juce::MessageManager::callAsync ([file] { file.startAsProcess(); });
            }
            void openPage() { auto page = pageUrl; juce::MessageManager::callAsync ([page] { juce::URL (page).launchInDefaultBrowser(); }); }
            juce::String url;
            std::function<void (float, const juce::String&)> report;
            juce::String pageUrl;
        };
        downloader = std::make_unique<Download> (r.installerUrl, report, r.pageUrl);
        downloader->startThread();
        return;
    }
   #endif
    juce::URL (r.pageUrl.isNotEmpty() ? r.pageUrl : "https://github.com/" + juce::String (repo) + "/releases/latest").launchInDefaultBrowser();
    if (report) report (1.0f, "Opened the release page");
}
