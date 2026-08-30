#pragma once

#include <JuceHeader.h>
#include "Logger.h"
#include <functional>
#include <queue>
#include <atomic>

class YoutubeDownloadManager : public juce::Thread {
public:
    struct VideoInfo {
        bool success = false;
        juce::String title;
        juce::String uploader;
        juce::String duration;
        juce::String url;
        juce::String errorMessage;
    };

    struct DownloadJob {
        juce::String url;
        bool playImmediately = false;
        juce::File destinationDir;
        std::function<void(bool success, const juce::File& file, const juce::String& title, const juce::String& error)> onComplete;
        std::function<void(const juce::String& status, float progress)> onProgress;
    };

    static YoutubeDownloadManager& getInstance() {
        static YoutubeDownloadManager instance;
        return instance;
    }

    static void fetchVideoInfoAsync(const juce::String& inputUrl, std::function<void(const VideoInfo&)> callback) {
        juce::Thread::launch([url = inputUrl.trim(), callback]() {
            VideoInfo info;
            info.url = url;

            if (url.isEmpty()) {
                info.errorMessage = "Empty URL";
                juce::MessageManager::callAsync([callback, info]() { if (callback) callback(info); });
                return;
            }

            // 1. Try YouTube oEmbed API first (<200ms quick response)
            if (url.contains("youtube.com") || url.contains("youtu.be")) {
                juce::URL oembedUrl("https://www.youtube.com/oembed?url=" + juce::URL::addEscapeChars(url, true) + "&format=json");
                std::unique_ptr<juce::InputStream> stream(oembedUrl.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(3000)));
                if (stream != nullptr) {
                    auto jsonStr = stream->readEntireStreamAsString();
                    auto parsed = juce::JSON::parse(jsonStr);
                    if (parsed.isObject()) {
                        info.title = parsed.getProperty("title", "").toString();
                        info.uploader = parsed.getProperty("author_name", "").toString();
                        if (info.title.isNotEmpty()) {
                            info.success = true;
                        }
                    }
                }
            }

            // 2. If oEmbed didn't work, use yt-dlp --print
            if (!info.success) {
                auto ytDlp = findYtDlpExe();
                if (ytDlp.existsAsFile()) {
                    juce::StringArray args;
                    args.add(ytDlp.getFullPathName());
                    args.add("--print");
                    args.add("%(title)s|||%(duration_string)s|||%(uploader)s");
                    args.add("--no-playlist");
                    args.add("--no-warnings");
                    args.add(url);

                    juce::ChildProcess p;
                    if (p.start(args, juce::ChildProcess::wantStdOut)) {
                        auto out = p.readAllProcessOutput().trim();
                        p.waitForProcessToFinish(6000);
                        if (out.isNotEmpty()) {
                            auto tokens = juce::StringArray::fromTokens(out, "|||", "");
                            if (tokens.size() > 0 && tokens[0].trim().isNotEmpty()) {
                                info.title = tokens[0].trim();
                                if (tokens.size() > 1) info.duration = tokens[1].trim();
                                if (tokens.size() > 2) info.uploader = tokens[2].trim();
                                info.success = true;
                            }
                        }
                    }
                }
            }

            if (!info.success && info.errorMessage.isEmpty()) {
                info.errorMessage = "Could not identify song title for this link.";
            }

            juce::MessageManager::callAsync([callback, info]() {
                if (callback) callback(info);
            });
        });
    }

    static juce::File getToolsDir() {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare").getChildFile("Tools");
        if (!dir.exists()) dir.createDirectory();
        return dir;
    }

    static juce::File getDefaultDownloadsDir() {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare").getChildFile("Downloads");
        if (!dir.exists()) dir.createDirectory();
        return dir;
    }

    juce::File getDownloadsDir() const {
        juce::ScopedLock sl(configLock);
        if (customDownloadsDir.isDirectory()) return customDownloadsDir;
        return getDefaultDownloadsDir();
    }

    void setDownloadsDir(const juce::File& dir) {
        if (dir.isDirectory()) {
            juce::ScopedLock sl(configLock);
            customDownloadsDir = dir;
            saveConfig();
        }
    }

    static void openFolderInExplorer(const juce::File& folder) {
        auto target = folder.isDirectory() ? folder : getDefaultDownloadsDir();
        if (target.exists()) {
            target.startAsProcess();
        }
    }

    static juce::File findYtDlpExe() {
        // 1. AppData Tools
        auto appDataExe = getToolsDir().getChildFile("yt-dlp.exe");
        if (appDataExe.existsAsFile()) return appDataExe;

        // 2. Next to executable
        auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        auto localExe = exeDir.getChildFile("yt-dlp.exe");
        if (localExe.existsAsFile()) return localExe;

        return appDataExe;
    }

    static juce::File findFfmpegExe() {
        auto appDataExe = getToolsDir().getChildFile("ffmpeg.exe");
        if (appDataExe.existsAsFile()) return appDataExe;

        auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        auto localExe = exeDir.getChildFile("ffmpeg.exe");
        if (localExe.existsAsFile()) return localExe;

        return appDataExe;
    }

    void queueDownload(const juce::String& url, bool playImmediately,
                       std::function<void(bool, const juce::File&, const juce::String&, const juce::String&)> onComplete,
                       std::function<void(const juce::String&, float)> onProgress = nullptr) {
        queueDownload(url, playImmediately, juce::File(), onComplete, onProgress);
    }

    void queueDownload(const juce::String& url, bool playImmediately, const juce::File& customTargetDir = {},
                       std::function<void(bool, const juce::File&, const juce::String&, const juce::String&)> onComplete = nullptr,
                       std::function<void(const juce::String&, float)> onProgress = nullptr) {
        if (url.trim().isEmpty()) return;

        DownloadJob job;
        job.url = url.trim();
        job.playImmediately = playImmediately;
        job.destinationDir = customTargetDir.isDirectory() ? customTargetDir : getDownloadsDir();
        job.onComplete = onComplete;
        job.onProgress = onProgress;

        {
            juce::ScopedLock sl(queueLock);
            jobQueue.push(job);
        }

        if (!isThreadRunning()) {
            startThread(juce::Thread::Priority::normal);
        } else {
            notify();
        }
    }

    bool isDownloading() const {
        return activeDownload.load();
    }

    juce::String getCurrentStatus() const {
        juce::ScopedLock sl(statusLock);
        return currentStatusText;
    }

private:
    YoutubeDownloadManager() : juce::Thread("YoutubeDownloadWorker") {
        loadConfig();
    }

    ~YoutubeDownloadManager() override {
        signalThreadShouldExit();
        notify();
        stopThread(3000);
    }

    void loadConfig() {
        auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare").getChildFile("settings").getChildFile("youtube_settings.json");
        if (file.existsAsFile()) {
            auto parsed = juce::JSON::parse(file);
            if (parsed.isObject()) {
                juce::String path = parsed.getProperty("downloadsFolder", "").toString();
                if (path.isNotEmpty()) {
                    juce::File dir(path);
                    if (dir.isDirectory()) {
                        customDownloadsDir = dir;
                    }
                }
            }
        }
    }

    void saveConfig() {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare").getChildFile("settings");
        if (!dir.exists()) dir.createDirectory();
        auto file = dir.getChildFile("youtube_settings.json");

        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("downloadsFolder", customDownloadsDir.getFullPathName());
        file.replaceWithText(juce::JSON::toString(juce::var(obj.get())));
    }

    void run() override {
        while (!threadShouldExit()) {
            DownloadJob job;
            bool hasJob = false;

            {
                juce::ScopedLock sl(queueLock);
                if (!jobQueue.empty()) {
                    job = jobQueue.front();
                    jobQueue.pop();
                    hasJob = true;
                }
            }

            if (hasJob) {
                activeDownload.store(true);
                processJob(job);
                activeDownload.store(false);
            } else {
                wait(500);
            }
        }
    }

    void setStatus(const juce::String& text, float progress, const DownloadJob& job) {
        {
            juce::ScopedLock sl(statusLock);
            currentStatusText = text;
        }
        if (job.onProgress) {
            juce::MessageManager::callAsync([cb = job.onProgress, text, progress]() {
                cb(text, progress);
            });
        }
    }

    void processJob(const DownloadJob& job) {
        setStatus("Initializing YouTube download...", 0.05f, job);

        auto ytDlp = findYtDlpExe();
        auto ffmpeg = findFfmpegExe();
        auto dlDir = job.destinationDir.isDirectory() ? job.destinationDir : getDownloadsDir();
        if (!dlDir.exists()) dlDir.createDirectory();

        // If yt-dlp.exe is missing, download it on the fly
        if (!ytDlp.existsAsFile()) {
            setStatus("Downloading yt-dlp tool...", 0.10f, job);
            auto url = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
            juce::URL downloadUrl(url);
            std::unique_ptr<juce::InputStream> inStream(downloadUrl.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)));
            if (inStream != nullptr) {
                juce::FileOutputStream outStream(ytDlp);
                if (outStream.openedOk()) {
                    outStream.writeFromInputStream(*inStream, -1);
                    outStream.flush();
                }
            }
        }

        if (!ytDlp.existsAsFile()) {
            juce::MessageManager::callAsync([job]() {
                if (job.onComplete) job.onComplete(false, juce::File(), "", "Could not locate or download yt-dlp.exe tool");
            });
            return;
        }

        setStatus("Fetching video audio (MP3 192k)...", 0.25f, job);

        // Build CLI command
        // yt-dlp -x --audio-format mp3 --audio-quality 192k --no-playlist --ffmpeg-location <dir> -o "<dlDir>/%(title)s.%(ext)s" "<url>"
        juce::StringArray args;
        args.add(ytDlp.getFullPathName());
        args.add("-x");
        args.add("--audio-format");
        args.add("mp3");
        args.add("--audio-quality");
        args.add("192k");
        args.add("--no-playlist");
        args.add("--no-warnings");

        if (ffmpeg.existsAsFile()) {
            args.add("--ffmpeg-location");
            args.add(ffmpeg.getParentDirectory().getFullPathName());
        }

        juce::String outTemplate = dlDir.getFullPathName() + "/%(title)s.%(ext)s";
        args.add("-o");
        args.add(outTemplate);

        args.add(job.url);

        juce::ChildProcess process;
        if (!process.start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
            juce::MessageManager::callAsync([job]() {
                if (job.onComplete) job.onComplete(false, juce::File(), "", "Failed to launch yt-dlp process");
            });
            return;
        }

        juce::String outputLog;
        juce::String detectedTitle;
        juce::File detectedFile;

        // Monitor process output
        while (process.isRunning() && !threadShouldExit()) {
            char buffer[512];
            int bytesRead = process.readProcessOutput(buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                juce::String chunk(buffer);
                outputLog += chunk;

                // Check for percentage progress
                if (chunk.contains("%")) {
                    int pctIdx = chunk.indexOf("%");
                    int startIdx = juce::jmax(0, pctIdx - 5);
                    juce::String sub = chunk.substring(startIdx, pctIdx).trim();
                    float pct = (float)sub.getDoubleValue();
                    if (pct > 0.0f) {
                        setStatus("Downloading: " + juce::String((int)pct) + "%", 0.3f + (pct * 0.005f), job);
                    }
                }

                if (chunk.contains("[ExtractAudio]")) {
                    setStatus("Converting to MP3 192k...", 0.90f, job);
                }
            }
            wait(50);
        }

        int exitCode = process.getExitCode();
        
        // Scan target directory for the newest mp3 file matching the timestamp
        auto files = dlDir.findChildFiles(juce::File::findFiles, false, "*.mp3");
        juce::File newestFile;
        juce::Time newestTime(0);

        for (const auto& f : files) {
            auto t = f.getLastModificationTime();
            if (t > newestTime) {
                newestTime = t;
                newestFile = f;
            }
        }

        bool success = (exitCode == 0 && newestFile.existsAsFile());
        if (success) {
            detectedFile = newestFile;
            detectedTitle = newestFile.getFileNameWithoutExtension();
            setStatus("Download complete: " + detectedTitle, 1.0f, job);
        } else {
            setStatus("Download failed (Exit code " + juce::String(exitCode) + ")", 0.0f, job);
        }

        juce::MessageManager::callAsync([job, success, detectedFile, detectedTitle, outputLog]() {
            if (job.onComplete) {
                job.onComplete(success, detectedFile, detectedTitle, success ? "" : outputLog.substring(outputLog.length() - 300));
            }
        });
    }

    mutable juce::CriticalSection configLock;
    juce::File customDownloadsDir;

    mutable juce::CriticalSection queueLock;
    std::queue<DownloadJob> jobQueue;

    mutable juce::CriticalSection statusLock;
    juce::String currentStatusText;
    std::atomic<bool> activeDownload{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(YoutubeDownloadManager)
};
