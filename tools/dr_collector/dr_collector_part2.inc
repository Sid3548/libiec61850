
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);

    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    data = (char*) malloc((size_t) size + 1);

    if (data == NULL) {
        fclose(fp);
        return NULL;
    }

    if (fread(data, 1, (size_t) size, fp) != (size_t) size) {
        free(data);
        fclose(fp);
        return NULL;
    }

    data[size] = 0;
    fclose(fp);
    return data;
}

static char*
findKey(char* start, const char* key)
{
    char pattern[160];
    char* pos;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(start, pattern);

    if (pos == NULL)
        return NULL;

    pos += strlen(pattern);

    while (*pos && isspace((unsigned char) *pos))
        pos++;

    if (*pos != ':')
        return NULL;

    pos++;

    while (*pos && isspace((unsigned char) *pos))
        pos++;

    return pos;
}

static bool
readJsonString(char** cursor, char* out, size_t outSize)
{
    char* p = *cursor;
    size_t o = 0;

    while (*p && isspace((unsigned char) *p))
        p++;

    if (*p != '"')
        return false;

    p++;

    while (*p && *p != '"') {
        char c = *p++;

        if (c == '\\' && *p) {
            c = *p++;
            if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
        }

        if (o + 1 < outSize)
            out[o++] = c;
    }

    if (*p != '"')
        return false;

    out[o] = 0;
    *cursor = p + 1;
    return true;
}

static void
readStringField(char* json, const char* key, char* out, size_t outSize)
{
    char* value = findKey(json, key);

    if (value)
        readJsonString(&value, out, outSize);
}

static int
readIntField(char* json, const char* key, int fallback)
{
    char* value = findKey(json, key);

    if (value == NULL)
        return fallback;

    return atoi(value);
}

static bool
readBoolField(char* json, const char* key, bool fallback)
{
    char* value = findKey(json, key);

    if (value == NULL)
        return fallback;

    if (strncmp(value, "true", 4) == 0)
        return true;

    if (strncmp(value, "false", 5) == 0)
        return false;

    return fallback;
}

static char*
findFirstRelayObject(char* json)
{
    char* relays = findKey(json, "relays");

    if (relays == NULL)
        return NULL;

    while (*relays && *relays != '{')
        relays++;

    return *relays == '{' ? relays : NULL;
}

static void
setDefaultConfig(CollectorConfig* config)
{
    memset(config, 0, sizeof(*config));
    copyString(config->outputDir, sizeof(config->outputDir), "%USERPROFILE%\\Desktop\\DRs");
    copyString(config->stateFile, sizeof(config->stateFile), "%USERPROFILE%\\Desktop\\DRs\\dr_state.tsv");
    copyString(config->relayName, sizeof(config->relayName), "relay-1");
    copyString(config->directory, sizeof(config->directory), "/COMTRADE/");
    config->relayPort = 102;
    config->stableCycles = 2;
    config->scanAttempts = 18;
    config->scanIntervalSec = 10;
    config->connectTimeoutMs = 5000;
    config->requestTimeoutMs = 20000;
    config->baselineExisting = true;
}

static bool
loadConfig(const char* path, CollectorConfig* config)
{
    char* json = readWholeFile(path);
    char* relay;

    setDefaultConfig(config);

    if (json == NULL) {
        logLine("ERROR", "cannot read config file");
        return false;
    }

    readStringField(json, "output_dir", config->outputDir, sizeof(config->outputDir));
    readStringField(json, "state_file", config->stateFile, sizeof(config->stateFile));
    config->stableCycles = readIntField(json, "stable_cycles_before_download", config->stableCycles);
    config->scanIntervalSec = readIntField(json, "post_trigger_scan_interval_sec", config->scanIntervalSec);
    config->scanAttempts = readIntField(json, "post_trigger_scan_attempts", config->scanAttempts);
    config->connectTimeoutMs = readIntField(json, "connect_timeout_ms", config->connectTimeoutMs);
    config->requestTimeoutMs = readIntField(json, "request_timeout_ms", config->requestTimeoutMs);
    config->baselineExisting = readBoolField(json, "baseline_existing_on_start", config->baselineExisting);

    relay = findFirstRelayObject(json);

    if (relay == NULL) {
        free(json);
        logLine("ERROR", "config has no relay object");
        return false;
    }

    readStringField(relay, "name", config->relayName, sizeof(config->relayName));
    readStringField(relay, "ip", config->relayIp, sizeof(config->relayIp));
    config->relayPort = readIntField(relay, "port", config->relayPort);
    readStringField(relay, "trigger_rcb", config->triggerRcb, sizeof(config->triggerRcb));
    readStringField(relay, "trigger_handler_reference", config->triggerHandler, sizeof(config->triggerHandler));
    readStringField(relay, "trigger_dataset", config->triggerDataSet, sizeof(config->triggerDataSet));

    if (config->triggerHandler[0] == 0)
        copyString(config->triggerHandler, sizeof(config->triggerHandler), config->triggerRcb);

    expandEnv(config->outputDir, sizeof(config->outputDir));
    expandEnv(config->stateFile, sizeof(config->stateFile));

    free(json);

    if (config->relayIp[0] == 0 || config->triggerRcb[0] == 0 ||
            strcmp(config->triggerRcb, "FILL_FROM_ICD_OR_RELAY_BROWSER") == 0) {
        logLine("ERROR", "fill relay ip and trigger_rcb in JSON before running");
        return false;
    }

    return true;
}

static int
asciiLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static bool
endsWithIgnoreCase(const char* value, const char* suffix)
{
    size_t valueLen = strlen(value);
    size_t suffixLen = strlen(suffix);
    size_t i;

    if (suffixLen > valueLen)
        return false;

    value += valueLen - suffixLen;

    for (i = 0; i < suffixLen; i++) {
        if (asciiLower((unsigned char) value[i]) != asciiLower((unsigned char) suffix[i]))
            return false;
    }

    return true;
}

static bool
isComtradeFile(const char* fileName)
{
    return endsWithIgnoreCase(fileName, ".cfg") || endsWithIgnoreCase(fileName, ".dat");
}

static const char*
baseFileName(const char* path)
{
    const char* base = path;
    const char* p;

    for (p = path; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':')
