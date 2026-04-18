    if (error != IED_ERROR_OK) {
        remove(part);
        snprintf(msg, sizeof(msg), "download failed %s: %s", file->remotePath, IedClientError_toString(error));
        logLine("ERROR", msg);
        return false;
    }

    remove(target);
    if (rename(part, target) != 0) {
        remove(part);
        return false;
    }

    snprintf(msg, sizeof(msg), "saved %s", target);
    logLine("INFO", msg);
    return true;
}

static bool
baseReady(const char* base, const RemoteFile* files, int count, const StateTable* state, int requiredStable)
{
    bool hasCfg = false;
    bool hasDat = false;
    bool hasNew = false;
    int i;

    for (i = 0; i < count; i++) {
        const FileState* item;

        if (strcmp(files[i].baseName, base) != 0 || files[i].stateIndex < 0)
            continue;

        item = &state->entries[files[i].stateIndex];

        if (endsWithIgnoreCase(files[i].localName, ".cfg"))
            hasCfg = true;
        if (endsWithIgnoreCase(files[i].localName, ".dat"))
            hasDat = true;
        if (!item->handled)
            hasNew = true;
        if (item->stableCount < requiredStable)
            return false;
    }

    return hasCfg && hasDat && hasNew;
}

static bool
downloadReadyGroups(IedConnection con, const CollectorConfig* config, RemoteFile* files, int count, StateTable* state)
{
    bool downloaded = false;
    int i;

    for (i = 0; i < count; i++) {
        char relayDir[MAX_PATH_LEN];
        char groupDir[MAX_PATH_LEN];
        char folder[128];
        char ts[64];
        int j;

        if (!baseReady(files[i].baseName, files, count, state, config->stableCycles))
            continue;

        formatTime(ts, sizeof(ts), newestTimeForBase(files[i].baseName, files, count));
        snprintf(folder, sizeof(folder), "dr_fault_%s", ts);
        joinLocalPath(relayDir, sizeof(relayDir), config->outputDir, config->relayName);
        joinLocalPath(groupDir, sizeof(groupDir), relayDir, folder);

        if (!makeDirectoryRecursive(groupDir)) {
            logLine("ERROR", "cannot create DR output directory");
            continue;
        }

        for (j = 0; j < count; j++) {
            if (strcmp(files[j].baseName, files[i].baseName) == 0) {
                if (!downloadOne(con, &files[j], groupDir))
                    return downloaded;
                state->entries[files[j].stateIndex].handled = true;
                state->entries[files[j].stateIndex].stableCount = 999;
            }
        }

        downloaded = true;
    }

    return downloaded;
}

static void
reportCallback(void* parameter, ClientReport report)
{
    TriggerContext* context = (TriggerContext*) parameter;
    char msg[MAX_PATH_LEN + 128];

    context->pending = 1;
    snprintf(msg, sizeof(msg), "%s: report trigger from %s", context->relayName, ClientReport_getRcbReference(report));
    logLine("INFO", msg);
}

static bool
enableReportTrigger(IedConnection con, const CollectorConfig* config, ClientReportControlBlock* outRcb, TriggerContext* context)
{
    IedClientError error;
    ClientReportControlBlock rcb;
    int parametersMask;
    char msg[MAX_PATH_LEN + 128];

    rcb = IedConnection_getRCBValues(con, &error, config->triggerRcb, NULL);

    if (error != IED_ERROR_OK || rcb == NULL) {
        snprintf(msg, sizeof(msg), "cannot read trigger RCB %s: %s", config->triggerRcb, IedClientError_toString(error));
        logLine("ERROR", msg);
        return false;
    }

    context->relayName = config->relayName;
    context->pending = 0;

    IedConnection_installReportHandler(con, config->triggerHandler, ClientReportControlBlock_getRptId(rcb),
            reportCallback, context);

    parametersMask = RCB_ELEMENT_RPT_ENA | RCB_ELEMENT_TRG_OPS;

#ifdef RCB_ELEMENT_RESV
    ClientReportControlBlock_setResv(rcb, true);
    parametersMask |= RCB_ELEMENT_RESV;
#endif

    ClientReportControlBlock_setTrgOps(rcb, TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED);
    if (config->triggerDataSet[0] != 0) {
        ClientReportControlBlock_setDataSetReference(rcb, config->triggerDataSet);
        parametersMask |= RCB_ELEMENT_DATSET;
    }
    ClientReportControlBlock_setRptEna(rcb, true);

    IedConnection_setRCBValues(con, &error, rcb, parametersMask, true);

    if (error != IED_ERROR_OK) {
        snprintf(msg, sizeof(msg), "cannot enable trigger RCB: %s", IedClientError_toString(error));
        ClientReportControlBlock_destroy(rcb);
        logLine("ERROR", msg);
        return false;
    }

    *outRcb = rcb;
    return true;
}

static int
runCollector(const CollectorConfig* config, StateTable* state, bool once)
{
    IedConnection con = IedConnection_create();
    IedClientError error;
    ClientReportControlBlock rcb = NULL;
    TriggerContext trigger;
    RemoteFile files[MAX_FILES];
    int count;
    char msg[256];

    IedConnection_setConnectTimeout(con, (uint32_t) config->connectTimeoutMs);
    IedConnection_setRequestTimeout(con, (uint32_t) config->requestTimeoutMs);

    snprintf(msg, sizeof(msg), "connect %s:%d", config->relayIp, config->relayPort);
    logLine("INFO", msg);

    IedConnection_connect(con, &error, config->relayIp, config->relayPort);
    if (error != IED_ERROR_OK) {
        snprintf(msg, sizeof(msg), "connect failed: %s", IedClientError_toString(error));
        logLine("ERROR", msg);
        IedConnection_destroy(con);
        return 1;
    }

    if (config->baselineExisting && state->count == 0) {
        count = scanComtrade(con, config, state, files, true);
        snprintf(msg, sizeof(msg), "baseline marked %d existing file(s)", count);
        logLine("INFO", msg);
        saveState(config->stateFile, state);
    }
    else if (config->baselineExisting) {
        logLine("INFO", "baseline skipped because state already has entries");
    }

    if (!enableReportTrigger(con, config, &rcb, &trigger)) {
        IedConnection_destroy(con);
        return 1;
    }

    logLine("INFO", "waiting for relay report trigger");

    while (gRunning && IedConnection_getState(con) == IED_STATE_CONNECTED) {
        if (trigger.pending) {
            int attempt;
            bool downloaded = false;

            trigger.pending = 0;

            for (attempt = 0; attempt < config->scanAttempts && gRunning; attempt++) {
                count = scanComtrade(con, config, state, files, false);
                downloaded = downloadReadyGroups(con, config, files, count, state);
                saveState(config->stateFile, state);

                if (downloaded)
                    break;

                sleepSeconds(config->scanIntervalSec);
            }

            if (once && downloaded)
                break;
        }

        sleepSeconds(1);
    }

    if (rcb) {
        int disableMask = RCB_ELEMENT_RPT_ENA;

        ClientReportControlBlock_setRptEna(rcb, false);
#ifdef RCB_ELEMENT_RESV
        ClientReportControlBlock_setResv(rcb, false);
        disableMask |= RCB_ELEMENT_RESV;
#endif
        IedConnection_setRCBValues(con, &error, rcb, disableMask, true);
        if (error != IED_ERROR_OK) {
            snprintf(msg, sizeof(msg), "cannot disable trigger RCB cleanly: %s", IedClientError_toString(error));
            logLine("WARN", msg);
        }
        ClientReportControlBlock_destroy(rcb);
    }
    IedConnection_abort(con, &error);
    IedConnection_destroy(con);
    return 0;
}

static void
printUsage(const char* program)
{
    printf("Usage: %s [config.json] [--once]\n", program);
    printf("Default config search:\n");
    printf("  %s\n", DEFAULT_CONFIG_FILE);
    printf("  tools/dr_collector/%s\n", DEFAULT_CONFIG_FILE);
    printf("  <program-dir>/%s\n", DEFAULT_CONFIG_FILE);
    printf("Edit first: tools/dr_collector/%s\n", DEFAULT_CONFIG_FILE);
}

static void
programDirPath(char* out, size_t outSize, const char* program, const char* fileName)
{
    char dir[MAX_PATH_LEN];
    char* slash;

    copyString(dir, sizeof(dir), program);
    slash = strrchr(dir, '/');

    if (slash == NULL)
        slash = strrchr(dir, '\\');

    if (slash == NULL) {
        copyString(out, outSize, fileName);
        return;
    }

    *slash = 0;
    joinLocalPath(out, outSize, dir, fileName);
}

static const char*
resolveDefaultConfig(char* out, size_t outSize, const char* program)
{
    char candidate[MAX_PATH_LEN];

    if (fileExists(DEFAULT_CONFIG_FILE)) {
        copyString(out, outSize, DEFAULT_CONFIG_FILE);
        return out;
    }

    joinLocalPath(candidate, sizeof(candidate), "tools/dr_collector", DEFAULT_CONFIG_FILE);
    if (fileExists(candidate)) {
        copyString(out, outSize, candidate);
        return out;
    }

    programDirPath(candidate, sizeof(candidate), program, DEFAULT_CONFIG_FILE);
    if (fileExists(candidate)) {
        copyString(out, outSize, candidate);
        return out;
    }

    if (fileExists(SAMPLE_CONFIG_FILE)) {
        copyString(out, outSize, SAMPLE_CONFIG_FILE);
        return out;
    }

    joinLocalPath(candidate, sizeof(candidate), "tools/dr_collector", SAMPLE_CONFIG_FILE);
    if (fileExists(candidate)) {
        copyString(out, outSize, candidate);
        return out;
    }

    programDirPath(candidate, sizeof(candidate), program, SAMPLE_CONFIG_FILE);
    if (fileExists(candidate)) {
        copyString(out, outSize, candidate);
        return out;
    }

    copyString(out, outSize, DEFAULT_CONFIG_FILE);
    return NULL;
}

#define RECONNECT_DELAY_SEC 30

int
main(int argc, char** argv)
{
    CollectorConfig config;
    StateTable state;
    bool once = false;
    const char* configPath = NULL;
    char defaultConfig[MAX_PATH_LEN];
    int result = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0)
            once = true;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        else if (configPath == NULL)
            configPath = argv[i];
        else {
            printUsage(argv[0]);
            return 1;
        }
    }

    if (configPath == NULL) {
        configPath = resolveDefaultConfig(defaultConfig, sizeof(defaultConfig), argv[0]);

        if (configPath == NULL) {
            printUsage(argv[0]);
            return 1;
        }
    }

    printf("Config: %s\n", configPath);

    if (!loadConfig(configPath, &config))
        return 1;

    if (!makeDirectoryRecursive(config.outputDir)) {
        logLine("ERROR", "cannot create output directory");
        return 1;
    }

    if (config.logFile[0] != 0) {
        char msg[MAX_PATH_LEN + 64];

        if (makeParentDirectory(config.logFile))
            gLogFile = fopen(config.logFile, "a");

        if (gLogFile) {
            snprintf(msg, sizeof(msg), "log file %s", config.logFile);
            logLine("INFO", msg);
        }
        else {
            logLine("WARN", "cannot open log file");
        }
    }

    if (!loadState(config.stateFile, &state))
        memset(&state, 0, sizeof(state));

    signal(SIGINT, handleSigint);

    while (gRunning) {
        result = runCollector(&config, &state, once);

        if (once || !gRunning)
            break;

        logLine("WARN", "connection lost — reconnecting in 30 seconds");
        sleepSeconds(RECONNECT_DELAY_SEC);
    }

    if (gLogFile) {
        fclose(gLogFile);
        gLogFile = NULL;
    }

    return result;
}
