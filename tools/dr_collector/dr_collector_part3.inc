            base = p + 1;
    }

    return base;
}

static void
deriveBaseName(char* out, size_t outSize, const char* fileName)
{
    char tmp[MAX_PATH_LEN];
    char* dot;

    copyString(tmp, sizeof(tmp), baseFileName(fileName));
    dot = strrchr(tmp, '.');

    if (dot)
        *dot = 0;

    copyString(out, outSize, tmp);
}

static void
joinRemotePath(char* out, size_t outSize, const char* dir, const char* file)
{
    size_t len;

    if (file[0] == '/' || file[0] == '\\') {
        copyString(out, outSize, file);
        return;
    }

    copyString(out, outSize, dir);
    len = strlen(out);

    if (len > 0 && out[len - 1] != '/' && out[len - 1] != '\\')
        snprintf(out + len, outSize - len, "/");

    len = strlen(out);
    snprintf(out + len, outSize - len, "%s", file);
}

static int
findState(StateTable* state, const char* remotePath)
{
    int i;

    for (i = 0; i < state->count; i++) {
        if (strcmp(state->entries[i].remotePath, remotePath) == 0)
            return i;
    }

    return -1;
}

static int
getState(StateTable* state, const char* remotePath)
{
    int index = findState(state, remotePath);

    if (index >= 0)
        return index;

    if (state->count >= MAX_STATE)
        return -1;

    index = state->count++;
    memset(&state->entries[index], 0, sizeof(state->entries[index]));
    copyString(state->entries[index].remotePath, sizeof(state->entries[index].remotePath), remotePath);
    return index;
}

static void
updateFileState(StateTable* state, RemoteFile* file, bool baseline)
{
    int index = getState(state, file->remotePath);
    FileState* item;

    file->stateIndex = index;

    if (index < 0)
        return;

    item = &state->entries[index];

    if (item->size == file->size && item->lastModified == file->lastModified) {
        if (item->stableCount < 999)
            item->stableCount++;
    }
    else {
        item->size = file->size;
        item->lastModified = file->lastModified;
        item->stableCount = 1;
        item->handled = false;
    }

    if (baseline) {
        item->handled = true;
        item->stableCount = 999;
    }
}

static bool
loadState(const char* path, StateTable* state)
{
    FILE* fp = fopen(path, "r");
    char line[MAX_PATH_LEN * 2];

    state->count = 0;

    if (fp == NULL)
        return false;

    while (fgets(line, sizeof(line), fp) && state->count < MAX_STATE) {
        char* remotePath = strtok(line, "\t\r\n");
        char* size = strtok(NULL, "\t\r\n");
        char* modified = strtok(NULL, "\t\r\n");
        char* handled = strtok(NULL, "\t\r\n");
        FileState* item;

        if (!remotePath || !size || !modified || !handled)
            continue;

        item = &state->entries[state->count++];
        memset(item, 0, sizeof(*item));
        copyString(item->remotePath, sizeof(item->remotePath), remotePath);
        item->size = (uint32_t) strtoul(size, NULL, 10);
        item->lastModified = (uint64_t) strtoull(modified, NULL, 10);
        item->handled = atoi(handled) != 0;
        item->stableCount = item->handled ? 999 : 1;
    }

    fclose(fp);
    return true;
}

static bool
saveState(const char* path, const StateTable* state)
{
    FILE* fp;
    char dir[MAX_PATH_LEN];
    char* slash;
    int i;

    copyString(dir, sizeof(dir), path);
    slash = strrchr(dir, '/');
    if (slash == NULL)
        slash = strrchr(dir, '\\');
    if (slash) {
        *slash = 0;
        makeDirectoryRecursive(dir);
    }

    fp = fopen(path, "w");
    if (fp == NULL)
        return false;

    for (i = 0; i < state->count; i++) {
        const FileState* item = &state->entries[i];
        fprintf(fp, "%s\t%u\t%llu\t%d\n", item->remotePath, item->size,
                (unsigned long long) item->lastModified, item->handled ? 1 : 0);
    }

    fclose(fp);
    return true;
}

static int
scanComtrade(IedConnection con, const CollectorConfig* config, StateTable* state, RemoteFile* files, bool baseline)
{
    IedClientError error;
    LinkedList list;
    LinkedList node;
    int count = 0;

    list = IedConnection_getFileDirectory(con, &error, config->directory);

    if (error != IED_ERROR_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg), "cannot list %s: %s", config->directory, IedClientError_toString(error));
        logLine("WARN", msg);
        return 0;
    }

    node = LinkedList_getNext(list);

    while (node && count < MAX_FILES) {
        FileDirectoryEntry entry = (FileDirectoryEntry) node->data;
        const char* name = FileDirectoryEntry_getFileName(entry);

        if (name && isComtradeFile(name)) {
            RemoteFile* file = &files[count++];
            memset(file, 0, sizeof(*file));
            joinRemotePath(file->remotePath, sizeof(file->remotePath), config->directory, name);
            copyString(file->localName, sizeof(file->localName), baseFileName(file->remotePath));
            deriveBaseName(file->baseName, sizeof(file->baseName), file->localName);
            file->size = FileDirectoryEntry_getFileSize(entry);
            file->lastModified = FileDirectoryEntry_getLastModified(entry);
            updateFileState(state, file, baseline);
        }

        node = LinkedList_getNext(node);
    }

    LinkedList_destroyDeep(list, (LinkedListValueDeleteFunction) FileDirectoryEntry_destroy);
    return count;
}

static uint64_t
newestTimeForBase(const char* base, const RemoteFile* files, int count)
{
    uint64_t newest = 0;
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(files[i].baseName, base) == 0 && files[i].lastModified > newest)
            newest = files[i].lastModified;
    }

    return newest;
}

static void
formatTime(char* out, size_t outSize, uint64_t ms)
{
    time_t seconds = ms ? (time_t) (ms / 1000) : time(NULL);
    struct tm* tmValue = localtime(&seconds);

    if (tmValue)
        strftime(out, outSize, "%Y%m%d_%H%M%S", tmValue);
    else
        copyString(out, outSize, "unknown_time");
}

static bool
writeDownloadChunk(void* parameter, uint8_t* buffer, uint32_t bytesRead)
{
    DownloadContext* context = (DownloadContext*) parameter;

    return bytesRead == 0 || fwrite(buffer, 1, bytesRead, context->fp) == bytesRead;
}

static bool
downloadOne(IedConnection con, const RemoteFile* file, const char* outDir)
{
    char target[MAX_PATH_LEN];
    char part[MAX_PATH_LEN];
    IedClientError error;
    DownloadContext context;
    char msg[MAX_PATH_LEN + 128];

    joinLocalPath(target, sizeof(target), outDir, file->localName);
    snprintf(part, sizeof(part), "%s.part", target);

    context.fp = fopen(part, "wb");
    if (context.fp == NULL)
        return false;

    IedConnection_getFile(con, &error, file->remotePath, writeDownloadChunk, &context);
    fclose(context.fp);

