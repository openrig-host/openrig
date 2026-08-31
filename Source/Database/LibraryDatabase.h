#pragma once

#include <JuceHeader.h>
#include <winsqlite/winsqlite3.h>

#pragma comment(lib, "winsqlite3.lib")

namespace Fanfare
{

struct TrackRecord
{
    juce::String filePath;
    juce::String fileName;
    juce::String title;
    juce::String artist;
    double durationSeconds = 0.0;
    float bpm = 0.0f;
    double firstBeatSeconds = 0.0;
    double cueInSeconds = 0.0;
    double cueOutSeconds = 0.0;
    float gainTrimDb = 0.0f;
    int playCount = 0;
    juce::Time lastPlayed;
    juce::Time dateAdded;

    double getEffectiveStart() const { return juce::jmax(0.0, cueInSeconds); }
    double getEffectiveEnd() const {
        return (cueOutSeconds > cueInSeconds && cueOutSeconds <= durationSeconds) ? cueOutSeconds : durationSeconds;
    }
    double getEffectiveDuration() const {
        double s = getEffectiveStart();
        double e = getEffectiveEnd();
        return (e > s) ? (e - s) : (durationSeconds > s ? (durationSeconds - s) : durationSeconds);
    }
    bool hasCues() const {
        return cueInSeconds > 0.0 || (cueOutSeconds > 0.0 && cueOutSeconds < durationSeconds);
    }
};

class LibraryDatabase
{
public:
    static LibraryDatabase& getInstance()
    {
        static LibraryDatabase instance;
        return instance;
    }

    ~LibraryDatabase()
    {
        close();
    }

    bool open(const juce::File& customFile = {})
    {
        juce::ScopedLock sl(dbLock);
        if (db != nullptr) return true;

        juce::File targetFile = customFile;
        if (targetFile == juce::File())
        {
            auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Fanfare");
            if (!appData.exists()) appData.createDirectory();
            targetFile = appData.getChildFile("fanfare_library.db");
        }

        dbPath = targetFile.getFullPathName();

        int rc = sqlite3_open_v2(dbPath.toRawUTF8(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
        if (rc != SQLITE_OK)
        {
            if (db != nullptr)
            {
                sqlite3_close_v2(db);
                db = nullptr;
            }
            return false;
        }

        // Enable Write-Ahead Logging (WAL) for high concurrency and crash resilience
        executeSqlInternal("PRAGMA journal_mode=WAL;");
        executeSqlInternal("PRAGMA synchronous=NORMAL;");
        executeSqlInternal("PRAGMA foreign_keys=ON;");

        createSchema();
        return true;
    }

    void close()
    {
        juce::ScopedLock sl(dbLock);
        if (db != nullptr)
        {
            sqlite3_close_v2(db);
            db = nullptr;
        }
    }

    bool isOpened() const
    {
        juce::ScopedLock sl(dbLock);
        return db != nullptr;
    }

    juce::File getDatabaseFile() const
    {
        return juce::File(dbPath);
    }

    // --- CRUD Operations ---
    bool upsertTrack(const TrackRecord& track)
    {
        if (track.filePath.isEmpty()) return false;
        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return false;

        const char* sql = "INSERT INTO tracks (file_path, file_name, title, artist, duration, bpm, first_beat_sec, cue_in_sec, cue_out_sec, gain_trim_db, play_count, last_played, date_added) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                          "ON CONFLICT(file_path) DO UPDATE SET "
                          "file_name=excluded.file_name, title=excluded.title, artist=excluded.artist, "
                          "duration=excluded.duration, bpm=excluded.bpm, first_beat_sec=excluded.first_beat_sec, "
                          "cue_in_sec=excluded.cue_in_sec, cue_out_sec=excluded.cue_out_sec, gain_trim_db=excluded.gain_trim_db, "
                          "play_count=excluded.play_count, last_played=excluded.last_played;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, track.filePath.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, track.fileName.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, track.title.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, track.artist.toRawUTF8(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, track.durationSeconds);
        sqlite3_bind_double(stmt, 6, (double)track.bpm);
        sqlite3_bind_double(stmt, 7, track.firstBeatSeconds);
        sqlite3_bind_double(stmt, 8, track.cueInSeconds);
        sqlite3_bind_double(stmt, 9, track.cueOutSeconds);
        sqlite3_bind_double(stmt, 10, (double)track.gainTrimDb);
        sqlite3_bind_int(stmt, 11, track.playCount);
        sqlite3_bind_int64(stmt, 12, track.lastPlayed.toMilliseconds());
        sqlite3_bind_int64(stmt, 13, track.dateAdded.toMilliseconds() > 0 ? track.dateAdded.toMilliseconds() : juce::Time::getCurrentTime().toMilliseconds());

        int res = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (res == SQLITE_DONE);
    }

    bool getTrack(const juce::String& filePath, TrackRecord& outRecord)
    {
        if (filePath.isEmpty()) return false;
        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return false;

        const char* sql = "SELECT file_path, file_name, title, artist, duration, bpm, first_beat_sec, cue_in_sec, cue_out_sec, gain_trim_db, play_count, last_played, date_added FROM tracks WHERE file_path = ? LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, filePath.toRawUTF8(), -1, SQLITE_TRANSIENT);

        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            readTrackFromRow(stmt, outRecord);
            found = true;
        }

        sqlite3_finalize(stmt);
        return found;
    }

    bool updateBpm(const juce::String& filePath, float bpm, double firstBeat)
    {
        if (filePath.isEmpty()) return false;
        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return false;

        TrackRecord existing;
        if (!getTrack(filePath, existing))
        {
            existing.filePath = filePath;
            existing.fileName = juce::File(filePath).getFileName();
            existing.title = juce::File(filePath).getFileNameWithoutExtension();
            existing.bpm = bpm;
            existing.firstBeatSeconds = firstBeat;
            existing.dateAdded = juce::Time::getCurrentTime();
            return upsertTrack(existing);
        }

        const char* sql = "UPDATE tracks SET bpm = ?, first_beat_sec = ? WHERE file_path = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_double(stmt, 1, (double)bpm);
        sqlite3_bind_double(stmt, 2, firstBeat);
        sqlite3_bind_text(stmt, 3, filePath.toRawUTF8(), -1, SQLITE_TRANSIENT);

        int res = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (res == SQLITE_DONE);
    }

    bool updateCues(const juce::String& filePath, double cueIn, double cueOut)
    {
        if (filePath.isEmpty()) return false;
        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return false;

        TrackRecord existing;
        if (!getTrack(filePath, existing))
        {
            existing.filePath = filePath;
            existing.fileName = juce::File(filePath).getFileName();
            existing.title = juce::File(filePath).getFileNameWithoutExtension();
            existing.cueInSeconds = cueIn;
            existing.cueOutSeconds = cueOut;
            existing.dateAdded = juce::Time::getCurrentTime();
            return upsertTrack(existing);
        }

        const char* sql = "UPDATE tracks SET cue_in_sec = ?, cue_out_sec = ? WHERE file_path = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_double(stmt, 1, cueIn);
        sqlite3_bind_double(stmt, 2, cueOut);
        sqlite3_bind_text(stmt, 3, filePath.toRawUTF8(), -1, SQLITE_TRANSIENT);

        int res = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (res == SQLITE_DONE);
    }

    bool incrementPlayCount(const juce::String& filePath)
    {
        if (filePath.isEmpty()) return false;
        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return false;

        const char* sql = "UPDATE tracks SET play_count = play_count + 1, last_played = ? WHERE file_path = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_int64(stmt, 1, juce::Time::getCurrentTime().toMilliseconds());
        sqlite3_bind_text(stmt, 2, filePath.toRawUTF8(), -1, SQLITE_TRANSIENT);

        int res = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (res == SQLITE_DONE);
    }

    std::vector<TrackRecord> getAllTracks(const juce::String& sortBy = "title")
    {
        std::vector<TrackRecord> tracks;
        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return tracks;

        juce::String col = "title ASC";
        if (sortBy == "bpm") col = "bpm ASC";
        else if (sortBy == "last_played") col = "last_played DESC";
        else if (sortBy == "play_count") col = "play_count DESC";

        juce::String sql = "SELECT file_path, file_name, title, artist, duration, bpm, first_beat_sec, cue_in_sec, cue_out_sec, gain_trim_db, play_count, last_played, date_added FROM tracks ORDER BY " + col + ";";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) != SQLITE_OK) return tracks;

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            TrackRecord tr;
            readTrackFromRow(stmt, tr);
            tracks.push_back(tr);
        }

        sqlite3_finalize(stmt);
        return tracks;
    }

    std::vector<TrackRecord> searchTracks(const juce::String& query, float minBpm = 0.0f, float maxBpm = 0.0f)
    {
        std::vector<TrackRecord> tracks;
        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return tracks;

        juce::String sql = "SELECT file_path, file_name, title, artist, duration, bpm, first_beat_sec, cue_in_sec, cue_out_sec, gain_trim_db, play_count, last_played, date_added FROM tracks WHERE 1=1 ";

        if (query.isNotEmpty())
        {
            sql += " AND (title LIKE ? OR artist LIKE ? OR file_name LIKE ?) ";
        }
        if (minBpm > 0.0f)
        {
            sql += " AND bpm >= " + juce::String(minBpm) + " ";
        }
        if (maxBpm > 0.0f)
        {
            sql += " AND bpm <= " + juce::String(maxBpm) + " ";
        }
        sql += " ORDER BY title ASC LIMIT 200;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.toRawUTF8(), -1, &stmt, nullptr) != SQLITE_OK) return tracks;

        if (query.isNotEmpty())
        {
            juce::String wild = "%" + query + "%";
            sqlite3_bind_text(stmt, 1, wild.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, wild.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, wild.toRawUTF8(), -1, SQLITE_TRANSIENT);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            TrackRecord tr;
            readTrackFromRow(stmt, tr);
            tracks.push_back(tr);
        }

        sqlite3_finalize(stmt);
        return tracks;
    }

    int getTrackCount()
    {
        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return 0;

        const char* sql = "SELECT COUNT(*) FROM tracks;";
        sqlite3_stmt* stmt = nullptr;
        int count = 0;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK)
        {
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                count = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        return count;
    }

    // --- JSON Migration ---
    void autoMigrateFromJson(const juce::File& jsonFile)
    {
        if (!jsonFile.existsAsFile()) return;

        auto parsed = juce::JSON::parse(jsonFile);
        if (!parsed.isObject()) return;

        auto* obj = parsed.getDynamicObject();
        if (obj == nullptr) return;

        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return;

        executeSqlInternal("BEGIN TRANSACTION;");
        for (const auto& prop : obj->getProperties())
        {
            juce::String filePath = prop.name.toString();
            if (auto* bo = prop.value.getDynamicObject())
            {
                TrackRecord tr;
                tr.filePath = filePath;
                tr.fileName = juce::File(filePath).getFileName();
                tr.title = juce::File(filePath).getFileNameWithoutExtension();
                tr.bpm = (float)bo->getProperty("bpm");
                tr.firstBeatSeconds = (double)bo->getProperty("firstBeat");
                tr.cueInSeconds = (double)bo->getProperty("cueIn");
                tr.cueOutSeconds = (double)bo->getProperty("cueOut");
                tr.dateAdded = juce::Time::getCurrentTime();
                upsertTrack(tr);
            }
        }
        executeSqlInternal("COMMIT;");
    }

    // --- Multi-Format Export Capabilities ---
    bool exportToCsv(const juce::File& targetFile)
    {
        juce::File file = targetFile.getFileExtension().isEmpty() ? targetFile.withFileExtension("csv") : targetFile;
        auto tracks = getAllTracks("title");
        juce::String csv = "File Path,File Name,Title,Artist,Duration (sec),BPM,First Beat (sec),Cue In (sec),Cue Out (sec),Effective Duration (sec),Gain Trim (dB),Play Count,Last Played,Date Added\r\n";

        auto escapeCsv = [](const juce::String& s) -> juce::String {
            if (s.containsAnyOf(",\"\n\r"))
            {
                return "\"" + s.replace("\"", "\"\"") + "\"";
            }
            return s;
        };

        for (const auto& t : tracks)
        {
            csv += escapeCsv(t.filePath) + ",";
            csv += escapeCsv(t.fileName) + ",";
            csv += escapeCsv(t.title) + ",";
            csv += escapeCsv(t.artist) + ",";
            csv += juce::String(t.durationSeconds, 2) + ",";
            csv += (t.bpm > 0.0f ? juce::String(t.bpm, 1) : "") + ",";
            csv += juce::String(t.firstBeatSeconds, 3) + ",";
            csv += juce::String(t.cueInSeconds, 2) + ",";
            csv += (t.cueOutSeconds > 0.0 ? juce::String(t.cueOutSeconds, 2) : "") + ",";
            csv += juce::String(t.getEffectiveDuration(), 2) + ",";
            csv += juce::String(t.gainTrimDb, 1) + ",";
            csv += juce::String(t.playCount) + ",";
            csv += (t.lastPlayed.toMilliseconds() > 0 ? t.lastPlayed.formatted("%Y-%m-%d %H:%M:%S") : "") + ",";
            csv += (t.dateAdded.toMilliseconds() > 0 ? t.dateAdded.formatted("%Y-%m-%d %H:%M:%S") : "");
            csv += "\r\n";
        }

        return file.replaceWithText(csv);
    }

    bool exportToJson(const juce::File& targetFile)
    {
        juce::File file = targetFile.getFileExtension().isEmpty() ? targetFile.withFileExtension("json") : targetFile;
        auto tracks = getAllTracks("title");
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("schemaVersion", "1.0");
        root->setProperty("exportDate", juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S"));
        root->setProperty("trackCount", (int)tracks.size());

        juce::Array<juce::var> arr;
        for (const auto& t : tracks)
        {
            juce::DynamicObject::Ptr to = new juce::DynamicObject();
            to->setProperty("filePath", t.filePath);
            to->setProperty("fileName", t.fileName);
            to->setProperty("title", t.title);
            to->setProperty("artist", t.artist);
            to->setProperty("durationSeconds", t.durationSeconds);
            to->setProperty("bpm", (double)t.bpm);
            to->setProperty("firstBeatSeconds", t.firstBeatSeconds);
            to->setProperty("cueInSeconds", t.cueInSeconds);
            to->setProperty("cueOutSeconds", t.cueOutSeconds);
            to->setProperty("effectiveDurationSeconds", t.getEffectiveDuration());
            to->setProperty("gainTrimDb", (double)t.gainTrimDb);
            to->setProperty("playCount", t.playCount);
            if (t.lastPlayed.toMilliseconds() > 0)
                to->setProperty("lastPlayed", t.lastPlayed.formatted("%Y-%m-%d %H:%M:%S"));
            if (t.dateAdded.toMilliseconds() > 0)
                to->setProperty("dateAdded", t.dateAdded.formatted("%Y-%m-%d %H:%M:%S"));
            arr.add(juce::var(to.get()));
        }

        root->setProperty("tracks", arr);
        return file.replaceWithText(juce::JSON::toString(juce::var(root.get())));
    }

    bool exportToM3u(const juce::File& targetFile, const std::vector<TrackRecord>& specificTracks = {})
    {
        juce::File file = targetFile.getFileExtension().isEmpty() ? targetFile.withFileExtension("m3u") : targetFile;
        auto tracks = specificTracks.empty() ? getAllTracks("title") : specificTracks;
        juce::String m3u = "#EXTM3U\n";
        m3u += "#EXT-FANFARE-EXPORT:date=" + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S") + "\n";

        for (const auto& t : tracks)
        {
            m3u += "#EXTINF:" + juce::String((int)std::round(t.durationSeconds)) + "," + (t.artist.isNotEmpty() ? (t.artist + " - ") : "") + t.title + "\n";
            if (t.bpm > 0.0f || t.hasCues())
            {
                m3u += "#EXT-FANFARE:bpm=" + juce::String(t.bpm, 1) + ";cue_in=" + juce::String(t.cueInSeconds, 2) + ";cue_out=" + juce::String(t.cueOutSeconds, 2) + "\n";
            }
            m3u += t.filePath + "\n";
        }

        return file.replaceWithText(m3u);
    }

    bool backupDatabase(const juce::File& targetFile)
    {
        juce::File file = targetFile.getFileExtension().isEmpty() ? targetFile.withFileExtension("db") : targetFile;
        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return false;

        sqlite3* backupDb = nullptr;
        int rc = sqlite3_open_v2(file.getFullPathName().toRawUTF8(), &backupDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK)
        {
            if (backupDb) sqlite3_close_v2(backupDb);
            return false;
        }

        sqlite3_backup* backup = sqlite3_backup_init(backupDb, "main", db, "main");
        if (backup == nullptr)
        {
            sqlite3_close_v2(backupDb);
            return false;
        }

        rc = sqlite3_backup_step(backup, -1);
        sqlite3_backup_finish(backup);
        sqlite3_close_v2(backupDb);
        return (rc == SQLITE_DONE);
    }

    bool importFromCsv(const juce::File& sourceFile)
    {
        if (!sourceFile.existsAsFile()) return false;
        juce::StringArray lines;
        sourceFile.readLines(lines);
        if (lines.size() <= 1) return false;

        juce::ScopedLock sl(dbLock);
        if (!ensureOpen()) return false;

        executeSqlInternal("BEGIN TRANSACTION;");
        for (int i = 1; i < lines.size(); ++i)
        {
            auto line = lines[i].trim();
            if (line.isEmpty()) continue;

            auto tokens = juce::StringArray::fromTokens(line, ",", "\"");
            if (tokens.size() >= 5)
            {
                TrackRecord tr;
                tr.filePath = tokens[0].trim();
                tr.fileName = tokens.size() > 1 ? tokens[1].trim() : juce::File(tr.filePath).getFileName();
                tr.title = tokens.size() > 2 ? tokens[2].trim() : juce::File(tr.filePath).getFileNameWithoutExtension();
                tr.artist = tokens.size() > 3 ? tokens[3].trim() : "";
                tr.durationSeconds = tokens.size() > 4 ? tokens[4].getDoubleValue() : 0.0;
                tr.bpm = tokens.size() > 5 ? (float)tokens[5].getDoubleValue() : 0.0f;
                tr.firstBeatSeconds = tokens.size() > 6 ? tokens[6].getDoubleValue() : 0.0;
                tr.cueInSeconds = tokens.size() > 7 ? tokens[7].getDoubleValue() : 0.0;
                tr.cueOutSeconds = tokens.size() > 8 ? tokens[8].getDoubleValue() : 0.0;
                tr.gainTrimDb = tokens.size() > 10 ? (float)tokens[10].getDoubleValue() : 0.0f;
                tr.playCount = tokens.size() > 11 ? tokens[11].getIntValue() : 0;
                tr.dateAdded = juce::Time::getCurrentTime();

                if (tr.filePath.isNotEmpty())
                {
                    upsertTrack(tr);
                }
            }
        }
        executeSqlInternal("COMMIT;");
        return true;
    }

private:
    LibraryDatabase() = default;

    bool ensureOpen()
    {
        if (db != nullptr) return true;
        return open();
    }

    bool executeSqlInternal(const char* sql)
    {
        if (db == nullptr) return false;
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (err != nullptr)
        {
            sqlite3_free(err);
        }
        return (rc == SQLITE_OK);
    }

    void createSchema()
    {
        const char* schema = 
            "CREATE TABLE IF NOT EXISTS tracks ("
            "    file_path        TEXT PRIMARY KEY,"
            "    file_name        TEXT NOT NULL,"
            "    title            TEXT,"
            "    artist           TEXT,"
            "    duration         REAL NOT NULL DEFAULT 0.0,"
            "    bpm              REAL DEFAULT 0.0,"
            "    first_beat_sec   REAL DEFAULT 0.0,"
            "    cue_in_sec       REAL DEFAULT 0.0,"
            "    cue_out_sec      REAL DEFAULT 0.0,"
            "    gain_trim_db     REAL DEFAULT 0.0,"
            "    play_count       INTEGER DEFAULT 0,"
            "    last_played      INTEGER DEFAULT 0,"
            "    date_added       INTEGER DEFAULT 0"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_tracks_bpm ON tracks(bpm);"
            "CREATE INDEX IF NOT EXISTS idx_tracks_title ON tracks(title);";

        executeSqlInternal(schema);
    }

    void readTrackFromRow(sqlite3_stmt* stmt, TrackRecord& out)
    {
        out.filePath = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 0));
        out.fileName = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 1));
        out.title = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 2));
        out.artist = juce::String::fromUTF8((const char*)sqlite3_column_text(stmt, 3));
        out.durationSeconds = sqlite3_column_double(stmt, 4);
        out.bpm = (float)sqlite3_column_double(stmt, 5);
        out.firstBeatSeconds = sqlite3_column_double(stmt, 6);
        out.cueInSeconds = sqlite3_column_double(stmt, 7);
        out.cueOutSeconds = sqlite3_column_double(stmt, 8);
        out.gainTrimDb = (float)sqlite3_column_double(stmt, 9);
        out.playCount = sqlite3_column_int(stmt, 10);
        out.lastPlayed = juce::Time(sqlite3_column_int64(stmt, 11));
        out.dateAdded = juce::Time(sqlite3_column_int64(stmt, 12));
    }

    mutable juce::CriticalSection dbLock;
    sqlite3* db = nullptr;
    juce::String dbPath;

    JUCE_DECLARE_NON_COPYABLE(LibraryDatabase)
};

} // namespace Fanfare
