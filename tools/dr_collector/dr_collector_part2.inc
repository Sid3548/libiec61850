
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

static bool
readIntField(char* json, const char* key, int fallback, int* outValue)
{
    char* value = findKey(json, key);
    char* endPtr;
    long parsed;

    if (value == NULL) {
        *outValue = fallback;
        return true;
    }

    errno = 0;
    parsed = strtol(value, &endPtr, 10);

    if (endPtr == value || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX)
        return false;

    while (*endPtr && isspace((unsigned char) *endPtr))
        endPtr++;

    if (*endPtr != 0 && *endPtr != ',' && *endPtr != '}' && *endPtr != ']')
        return false;

    *outValue = (int) parsed;
    return true;
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

static int
clampIntMin(int value, int minValue)
{
    return value < minValue ? minValue : value;
}

static int
clampIntRange(int value, int minValue, int maxValue)
{
    if (value < minValue)
        return minValue;

    if (value > maxValue)
        return maxValue;

    return value;
}

static bool
isConfigPlaceholder(const char* value)
{
    return value == NULL || value[0] == 0 || strcmp(value, "FILL_FROM_ICD_OR_RELAY_BROWSER") == 0;
}

static char*
findObjectField(char* json, const char* key)
{
    char* object = findKey(json, key);

    if (object == NULL)
        return NULL;

    while (*object && *object != '{')
        object++;

    return *object == '{' ? object : NULL;
}

static char*
findRelayObject(char* json)
{
    char* relay = findObjectField(json, "relay");

    if (relay)
        return relay;

    return findObjectField(json, "relays");
}

static void
setDefaultConfig(CollectorConfig* config)
{
    memset(config, 0, sizeof(*config));
    copyString(config->outputDir, sizeof(config->outputDir), "%USERPROFILE%\\Desktop\\DRs");
    copyString(config->stateFile, sizeof(config->stateFile), "%USERPROFILE%\\Desktop\\DRs\\dr_state.tsv");
    copyString(config->logFile, sizeof(config->logFile), "%USERPROFILE%\\Desktop\\DRs\\dr_collector.log");
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
    int parsedValue;

    setDefaultConfig(config);

    if (json == NULL) {
        char msg[MAX_PATH_LEN + 64];
        snprintf(msg, sizeof(msg), "cannot read config file %s", path);
        logLine("ERROR", msg);
        return false;
    }

    readStringField(json, "output_dir", config->outputDir, sizeof(config->outputDir));
    readStringField(json, "state_file", config->stateFile, sizeof(config->stateFile));
    readStringField(json, "log_file", config->logFile, sizeof(config->logFile));
    if (!readIntField(json, "stable_cycles_before_download", config->stableCycles, &parsedValue)) {
        free(json);
        logLine("ERROR", "invalid stable_cycles_before_download");
        return false;
    }
    config->stableCycles = parsedValue;
    if (!readIntField(json, "post_trigger_scan_interval_sec", config->scanIntervalSec, &parsedValue)) {
        free(json);
        logLine("ERROR", "invalid post_trigger_scan_interval_sec");
        return false;
    }
    config->scanIntervalSec = parsedValue;
    if (!readIntField(json, "post_trigger_scan_attempts", config->scanAttempts, &parsedValue)) {
        free(json);
        logLine("ERROR", "invalid post_trigger_scan_attempts");
        return false;
    }
    config->scanAttempts = parsedValue;
    if (!readIntField(json, "connect_timeout_ms", config->connectTimeoutMs, &parsedValue)) {
        free(json);
        logLine("ERROR", "invalid connect_timeout_ms");
        return false;
    }
    config->connectTimeoutMs = parsedValue;
    if (!readIntField(json, "request_timeout_ms", config->requestTimeoutMs, &parsedValue)) {
        free(json);
        logLine("ERROR", "invalid request_timeout_ms");
        return false;
    }
    config->requestTimeoutMs = parsedValue;
    config->baselineExisting = readBoolField(json, "baseline_existing_on_start", config->baselineExisting);

    relay = findRelayObject(json);

    if (relay == NULL) {
        free(json);
        logLine("ERROR", "config has no relay object");
        return false;
    }

    readStringField(relay, "name", config->relayName, sizeof(config->relayName));
    readStringField(relay, "ip", config->relayIp, sizeof(config->relayIp));
    if (!readIntField(relay, "port", config->relayPort, &parsedValue)) {
        free(json);
        logLine("ERROR", "invalid relay port");
        return false;
    }
    config->relayPort = parsedValue;
    readStringField(relay, "trigger_rcb", config->triggerRcb, sizeof(config->triggerRcb));
    readStringField(relay, "trigger_handler_reference", config->triggerHandler, sizeof(config->triggerHandler));
    readStringField(relay, "trigger_dataset", config->triggerDataSet, sizeof(config->triggerDataSet));
    readStringField(relay, "directory", config->directory, sizeof(config->directory));

    config->stableCycles = clampIntMin(config->stableCycles, 1);
    config->scanIntervalSec = clampIntMin(config->scanIntervalSec, 1);
    config->scanAttempts = clampIntMin(config->scanAttempts, 1);
    config->connectTimeoutMs = clampIntMin(config->connectTimeoutMs, 1000);
    config->requestTimeoutMs = clampIntMin(config->requestTimeoutMs, 1000);
    config->relayPort = clampIntRange(config->relayPort, 1, 65535);

    if (config->directory[0] == 0)
        copyString(config->directory, sizeof(config->directory), "/COMTRADE/");

    if (isConfigPlaceholder(config->triggerHandler))
        copyString(config->triggerHandler, sizeof(config->triggerHandler), config->triggerRcb);

    expandEnv(config->outputDir, sizeof(config->outputDir));
    expandEnv(config->stateFile, sizeof(config->stateFile));
    expandEnv(config->logFile, sizeof(config->logFile));

    free(json);

    if (config->relayIp[0] == 0 || isConfigPlaceholder(config->triggerRcb)) {
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