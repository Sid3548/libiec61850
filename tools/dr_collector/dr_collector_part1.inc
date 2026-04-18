/*
 * dr_collector.c
 *
 * MVP IEC 61850/MMS disturbance-record collector for one GRL200 bench relay.
 * It waits for a configured report-control-block trigger, then downloads new
 * stable COMTRADE .cfg/.dat files from /COMTRADE/. No GOOSE. No delete.
 */

#include "iec61850_client.h"
#include "linked_list.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define LOCAL_SEP '\\'
#define MKDIR(path) _mkdir(path)
static void sleepSeconds(int seconds) { Sleep((DWORD) seconds * 1000); }
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define LOCAL_SEP '/'
#define MKDIR(path) mkdir(path, 0777)
static void sleepSeconds(int seconds) { sleep((unsigned int) seconds); }
#endif

#define MAX_PATH_LEN 1024
#define MAX_NAME_LEN 128
#define MAX_FILES 512
#define MAX_STATE 2048
#define MAX_GROUP_FILES 8
#define DEFAULT_CONFIG_FILE "dr_collector.local.json"
#define SAMPLE_CONFIG_FILE "dr_collector.sample.json"

typedef struct {
    char outputDir[MAX_PATH_LEN];
    char stateFile[MAX_PATH_LEN];
    char logFile[MAX_PATH_LEN];
    char relayName[MAX_NAME_LEN];
    char relayIp[MAX_NAME_LEN];
    int relayPort;
    char triggerRcb[MAX_PATH_LEN];
    char triggerHandler[MAX_PATH_LEN];
    char triggerDataSet[MAX_PATH_LEN];
    char directory[MAX_PATH_LEN];
    int stableCycles;
    int scanAttempts;
    int scanIntervalSec;
    int connectTimeoutMs;
    int requestTimeoutMs;
    bool baselineExisting;
} CollectorConfig;

typedef struct {
    char remotePath[MAX_PATH_LEN];
    char localName[MAX_PATH_LEN];
    char baseName[MAX_PATH_LEN];
    uint32_t size;
    uint64_t lastModified;
    int stateIndex;
} RemoteFile;

typedef struct {
    char remotePath[MAX_PATH_LEN];
    uint32_t size;
    uint64_t lastModified;
    int stableCount;
    bool handled;
} FileState;

typedef struct {
    FileState entries[MAX_STATE];
    int count;
} StateTable;

typedef struct {
    const char* relayName;
    volatile int pending;
} TriggerContext;

typedef struct {
    FILE* fp;
} DownloadContext;

static volatile int gRunning = 1;
static FILE* gLogFile = NULL;

static void
handleSigint(int signalId)
{
    (void) signalId;
    gRunning = 0;
}

static void
copyString(char* dst, size_t dstSize, const char* src)
{
    if (dstSize == 0)
        return;

    if (src == NULL)
        src = "";

    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = 0;
}

static void
logLine(const char* level, const char* message)
{
    time_t now = time(NULL);
    struct tm* tmNow = localtime(&now);
    char ts[32];

    if (tmNow)
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tmNow);
    else
        copyString(ts, sizeof(ts), "unknown-time");

    printf("%s [%s] %s\n", ts, level, message);
    fflush(stdout);

    if (gLogFile) {
        fprintf(gLogFile, "%s [%s] %s\n", ts, level, message);
        fflush(gLogFile);
    }
}

static bool
isLocalSep(char c)
{
    return c == '/' || c == '\\';
}

static bool
isDrivePrefix(const char* path, size_t index)
{
    return index == 2 && isalpha((unsigned char) path[0]) && path[1] == ':';
}

static bool
directoryExists(const char* path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool
makeDirectoryRecursive(const char* path)
{
    char tmp[MAX_PATH_LEN];
    size_t len;
    size_t i;

    if (path == NULL || path[0] == 0)
        return true;

    copyString(tmp, sizeof(tmp), path);
    len = strlen(tmp);

    while (len > 0 && isLocalSep(tmp[len - 1]))
        tmp[--len] = 0;

    for (i = 1; i < len; i++) {
        if (isLocalSep(tmp[i])) {
            char saved = tmp[i];
            tmp[i] = 0;

            if (!isDrivePrefix(tmp, i) && tmp[0] != 0 && !directoryExists(tmp)) {
                if (MKDIR(tmp) != 0 && errno != EEXIST) {
                    tmp[i] = saved;
                    return false;
                }
            }

            tmp[i] = saved;
        }
    }

    return directoryExists(tmp) || MKDIR(tmp) == 0 || errno == EEXIST;
}

static bool
makeParentDirectory(const char* path)
{
    char dir[MAX_PATH_LEN];
    char* slash;

    copyString(dir, sizeof(dir), path);
    slash = strrchr(dir, '/');

    if (slash == NULL)
        slash = strrchr(dir, '\\');

    if (slash == NULL)
        return true;

    *slash = 0;
    return makeDirectoryRecursive(dir);
}

static void
joinLocalPath(char* out, size_t outSize, const char* left, const char* right)
{
    size_t len;

    copyString(out, outSize, left);
    len = strlen(out);

    if (len > 0 && !isLocalSep(out[len - 1]))
        snprintf(out + len, outSize - len, "%c", LOCAL_SEP);

    len = strlen(out);
    snprintf(out + len, outSize - len, "%s", right ? right : "");
}

static bool
fileExists(const char* path)
{
    FILE* fp = fopen(path, "rb");

    if (fp == NULL)
        return false;

    fclose(fp);
    return true;
}

static void
expandHomePrefix(char* path, size_t pathSize)
{
    const char* home;
    char out[MAX_PATH_LEN];

    if (path[0] != '~' || (path[1] != 0 && !isLocalSep(path[1])))
        return;

    home = getenv("HOME");

    if (home == NULL)
        home = getenv("USERPROFILE");

    if (home == NULL || home[0] == 0)
        return;

    snprintf(out, sizeof(out), "%s%s", home, path + 1);
    copyString(path, pathSize, out);
}

static void
expandEnv(char* path, size_t pathSize)
{
    char out[MAX_PATH_LEN];
    size_t i = 0;
    size_t o = 0;

    while (path[i] && o + 1 < sizeof(out)) {
        if (path[i] == '%') {
            size_t end = i + 1;

            while (path[end] && path[end] != '%')
                end++;

            if (path[end] == '%') {
                char name[128];
                const char* value;
                size_t n = end - i - 1;

                if (n >= sizeof(name))
                    n = sizeof(name) - 1;

                memcpy(name, path + i + 1, n);
                name[n] = 0;
                value = getenv(name);

                if (value) {
                    size_t v = strlen(value);
                    if (o + v >= sizeof(out))
                        v = sizeof(out) - o - 1;
                    memcpy(out + o, value, v);
                    o += v;
                }
                else {
                    size_t literal = end - i + 1;
                    if (o + literal >= sizeof(out))
                        literal = sizeof(out) - o - 1;
                    memcpy(out + o, path + i, literal);
                    o += literal;
                }

                i = end + 1;
                continue;
            }
        }

        out[o++] = path[i++];
    }

    out[o] = 0;
    copyString(path, pathSize, out);
    expandHomePrefix(path, pathSize);
}

static char*
readWholeFile(const char* path)
{
    FILE* fp = fopen(path, "rb");
    long size;
    char* data;

    if (fp == NULL)
        return NULL;