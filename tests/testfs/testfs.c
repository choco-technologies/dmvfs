#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod.h"
#include "dmfsi.h"

// Lokalna implementacja funkcji string/mem
void testfs_memset(void* ptr, int value, size_t num) {
    unsigned char* p = (unsigned char*)ptr;
    for (size_t i = 0; i < num; ++i) p[i] = (unsigned char)value;
}

void testfs_memcpy(void* dest, const void* src, size_t num) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < num; ++i) d[i] = s[i];
}

void testfs_strcpy(char* dest, const char* src) {
    while ((*dest++ = *src++));
}

void testfs_strncpy(char* dest, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; ++i) dest[i] = src[i];
    for (; i < n; ++i) dest[i] = '\0';
}

int testfs_strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { ++a; ++b; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

const char* testfs_strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        ++s;
    }
    return last;
}

void testfs_memmove(void* dest, const void* src, size_t num) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        for (size_t i = 0; i < num; ++i) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = num; i > 0; --i) d[i-1] = s[i-1];
    }
}

#define TESTFS_MAX_FILES 32
#define TESTFS_MAX_FILE_SIZE 4096
#define TESTFS_MAX_FILENAME 128
#define TESTFS_MAX_DIRS 16
#define TESTFS_MAX_DIRNAME 128
#define TESTFS_MAX_DIR_ENTRIES 32

typedef struct {
    char name[TESTFS_MAX_FILENAME];
    char data[TESTFS_MAX_FILE_SIZE];
    dmfsi_size_t size;
    int attr;
    int used;
} testfs_file_t;

typedef struct {
    char name[TESTFS_MAX_DIRNAME];
    int file_indices[TESTFS_MAX_DIR_ENTRIES];
    int file_count;
    int used;
} testfs_dir_t;

typedef struct {
    testfs_file_t files[TESTFS_MAX_FILES];
    testfs_dir_t dirs[TESTFS_MAX_DIRS];
    int file_count;
    int dir_count;
} testfs_context_t;

struct dmfsi_context {
    testfs_context_t ramfs;
};

// File handle
typedef struct {
    int file_index;
    dmfsi_offset_t pos;
    int open;
} testfs_fp_t;

// Directory handle
typedef struct {
    int dir_index;
    int entry_pos;
    int open;
} testfs_dp_t;

/**
 * @brief Pre-initialization function for the module.
 * 
 * @note This function is optional. You can remove it if you don't need it.
 * 
 * This function is called when the module enabling is in progress.
 * 
 * You can use this function to load the required dependencies, such as 
 * other modules. Please be aware that the module is not fully initialized, 
 * so not all the API functions are available - you can check if the API
 * is connected by calling the Dmod_IsFunctionConnected() function.
 */
void dmod_preinit(void)
{
    if(Dmod_IsFunctionConnected( Dmod_Printf ))
    {
        Dmod_Printf("API is connected!\n");
    }
}

/**
 * @brief Initialization function for the module.
 * 
 * This function is called when the module is enabled.
 * Please use this function to initialize the module, for instance:
 * - initialize the module variables
 * - initialize the module hardware
 * - allocate memory
 */
int dmod_init(const Dmod_Config_t *Config)
{
    Dmod_Printf("testfs initialized\n");
    return 0;
}

/**
 * @brief De-initialization function for the module.
 * 
 * This function is called when the module is disabled.
 * Please use this function to de-initialize the module, for instance:
 * - free memory
 * - de-initialize the module hardware
 * - de-initialize the module variables
 */
int dmod_deinit(void)
{
    Dmod_Printf("testfs deinitialized!\n");
    return 0;
}

// Helper: find directory by path
static int testfs_find_dir(testfs_context_t* fs, const char* path) {
    for (int i = 0; i < TESTFS_MAX_DIRS; ++i) {
        if (fs->dirs[i].used && testfs_strcmp(fs->dirs[i].name, path) == 0)
            return i;
    }
    return -1;
}

// Helper: find parent directory and filename from path
static int testfs_split_path(const char* path, char* dir_out, char* file_out) {
    const char* last_slash = testfs_strrchr(path, '/');
    if (!last_slash) return 0;
    size_t dir_len = last_slash - path;
    if (dir_len == 0) dir_len = 1; // root
    testfs_strncpy(dir_out, path, dir_len);
    dir_out[dir_len] = '\0';
    testfs_strncpy(file_out, last_slash + 1, TESTFS_MAX_FILENAME);
    return 1;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, dmfsi_context_t, _init, (const char* config) )
{
    struct dmfsi_context* ctx = (struct dmfsi_context*)Dmod_Malloc(sizeof(struct dmfsi_context));
    if (!ctx) return NULL;
    testfs_memset(ctx, 0, sizeof(struct dmfsi_context));
    // root dir
    testfs_strcpy(ctx->ramfs.dirs[0].name, "/");
    ctx->ramfs.dirs[0].used = 1;
    ctx->ramfs.dir_count = 1;
    return ctx;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _deinit, (dmfsi_context_t ctx) )
{
    if (!ctx) return DMFSI_ERR_INVALID;
    Dmod_Free(ctx);
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _context_is_valid, (dmfsi_context_t ctx) )
{
    return ctx != NULL;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _fopen, (dmfsi_context_t ctx, void** fp, const char* path, int mode, int attr) )
{
    if (!ctx || !fp || !path) return DMFSI_ERR_INVALID;
    testfs_context_t* fs = &ctx->ramfs;
    char dir_path[TESTFS_MAX_DIRNAME];
    char file_name[TESTFS_MAX_FILENAME];
    int has_dir = testfs_split_path(path, dir_path, file_name);
    int dir_index = has_dir ? testfs_find_dir(fs, dir_path) : 0;
    if (dir_index == -1) return DMFSI_ERR_NOT_FOUND;
    int file_index = -1;
    for (int i = 0; i < TESTFS_MAX_FILES; ++i) {
        if (fs->files[i].used && testfs_strcmp(fs->files[i].name, path) == 0) {
            file_index = i;
            break;
        }
    }
    if (file_index == -1) {
        if (mode & DMFSI_O_CREAT) {
            for (int i = 0; i < TESTFS_MAX_FILES; ++i) {
                if (!fs->files[i].used) {
                    testfs_strncpy(fs->files[i].name, path, TESTFS_MAX_FILENAME);
                    fs->files[i].size = 0;
                    fs->files[i].attr = attr;
                    fs->files[i].used = 1;
                    file_index = i;
                    fs->file_count++;
                    // Add to directory
                    testfs_dir_t* dir = &fs->dirs[dir_index];
                    if (dir->file_count < TESTFS_MAX_DIR_ENTRIES) {
                        dir->file_indices[dir->file_count++] = file_index;
                    }
                    break;
                }
            }
            if (file_index == -1) return DMFSI_ERR_NO_SPACE;
        } else {
            return DMFSI_ERR_NOT_FOUND;
        }
    }
    testfs_fp_t* handle = (testfs_fp_t*)Dmod_Malloc(sizeof(testfs_fp_t));
    if (!handle) return DMFSI_ERR_GENERAL;
    handle->file_index = file_index;
    handle->pos = (mode & DMFSI_O_APPEND) ? fs->files[file_index].size : 0;
    handle->open = 1;
    *fp = handle;
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _fclose, (dmfsi_context_t ctx, void* fp) )
{
    if (!ctx || !fp) return DMFSI_ERR_INVALID;
    testfs_fp_t* handle = (testfs_fp_t*)fp;
    handle->open = 0;
    Dmod_Free(handle);
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _fread, (dmfsi_context_t ctx, void* fp, void* buffer, size_t size, size_t* read) )
{
    if (!ctx || !fp || !buffer || !read) return DMFSI_ERR_INVALID;
    testfs_fp_t* handle = (testfs_fp_t*)fp;
    testfs_context_t* fs = &ctx->ramfs;
    if (!fs->files[handle->file_index].used) return DMFSI_ERR_NOT_FOUND;
    size_t available = fs->files[handle->file_index].size - handle->pos;
    size_t to_read = (size < available) ? size : available;
    testfs_memcpy(buffer, fs->files[handle->file_index].data + handle->pos, to_read);
    handle->pos += to_read;
    *read = to_read;
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _fwrite, (dmfsi_context_t ctx, void* fp, const void* buffer, size_t size, size_t* written) )
{
    if (!ctx || !fp || !buffer || !written) return DMFSI_ERR_INVALID;
    testfs_fp_t* handle = (testfs_fp_t*)fp;
    testfs_context_t* fs = &ctx->ramfs;
    if (!fs->files[handle->file_index].used) return DMFSI_ERR_NOT_FOUND;
    size_t available = TESTFS_MAX_FILE_SIZE - handle->pos;
    size_t to_write = (size < available) ? size : available;
    testfs_memcpy(fs->files[handle->file_index].data + handle->pos, buffer, to_write);
    handle->pos += to_write;
    if (handle->pos > fs->files[handle->file_index].size)
        fs->files[handle->file_index].size = handle->pos;
    *written = to_write;
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 2.0, testfs, dmfsi_size_t, _lseek, (dmfsi_context_t ctx, void* fp, dmfsi_size_t offset, int whence) )
{
    if (!ctx || !fp) return DMFSI_ERR_INVALID;
    testfs_fp_t* handle = (testfs_fp_t*)fp;
    testfs_context_t* fs = &ctx->ramfs;
    size_t new_pos = 0;
    switch (whence) {
        case DMFSI_SEEK_SET: new_pos = offset; break;
        case DMFSI_SEEK_CUR: new_pos = handle->pos + offset; break;
        case DMFSI_SEEK_END: new_pos = fs->files[handle->file_index].size + offset; break;
        default: return DMFSI_ERR_INVALID;
    }
    if (new_pos > fs->files[handle->file_index].size) return DMFSI_ERR_INVALID;
    handle->pos = new_pos;
    return (dmfsi_size_t)new_pos;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _ioctl, (dmfsi_context_t ctx, void* fp, int request, void* arg) )
{
    return 0;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _sync, (dmfsi_context_t ctx, void* fp) )
{
    return 0;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _getc, (dmfsi_context_t ctx, void* fp) )
{
    if (!ctx || !fp) return DMFSI_ERR_INVALID;
    testfs_fp_t* handle = (testfs_fp_t*)fp;
    testfs_context_t* fs = &ctx->ramfs;
    if (handle->pos >= fs->files[handle->file_index].size) return -1;
    return (unsigned char)fs->files[handle->file_index].data[handle->pos++];
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _putc, (dmfsi_context_t ctx, void* fp, int c) )
{
    if (!ctx || !fp) return DMFSI_ERR_INVALID;
    testfs_fp_t* handle = (testfs_fp_t*)fp;
    testfs_context_t* fs = &ctx->ramfs;
    if (handle->pos >= TESTFS_MAX_FILE_SIZE) return -1;
    fs->files[handle->file_index].data[handle->pos++] = (char)c;
    if (handle->pos > fs->files[handle->file_index].size)
        fs->files[handle->file_index].size = handle->pos;
    return c;
}

dmod_dmfsi_dif_api_declaration( 2.0, testfs, dmfsi_offset_t, _tell, (dmfsi_context_t ctx, void* fp) )
{
    if (!ctx || !fp) return DMFSI_ERR_INVALID;
    testfs_fp_t* handle = (testfs_fp_t*)fp;
    return (dmfsi_offset_t)handle->pos;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _eof, (dmfsi_context_t ctx, void* fp) )
{
    if (!ctx || !fp) return DMFSI_ERR_INVALID;
    testfs_fp_t* handle = (testfs_fp_t*)fp;
    testfs_context_t* fs = &ctx->ramfs;
    if (handle->pos >= fs->files[handle->file_index].size) return 1;
    return 0;
}

dmod_dmfsi_dif_api_declaration( 2.0, testfs, dmfsi_size_t, _size, (dmfsi_context_t ctx, void* fp) )
{
    if (!ctx || !fp) return DMFSI_ERR_INVALID;
    testfs_fp_t* handle = (testfs_fp_t*)fp;
    testfs_context_t* fs = &ctx->ramfs;
    return (long)fs->files[handle->file_index].size;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _fflush, (dmfsi_context_t ctx, void* fp) )
{
    // No-op for RAM
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _error, (dmfsi_context_t ctx, void* fp) )
{
    // No error tracking
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _opendir, (dmfsi_context_t ctx, void** dp, const char* path) )
{
    if (!ctx || !dp || !path) return DMFSI_ERR_INVALID;
    testfs_context_t* fs = &ctx->ramfs;
    int dir_index = testfs_find_dir(fs, path);
    if (dir_index == -1) return DMFSI_ERR_NOT_FOUND;
    testfs_dp_t* handle = (testfs_dp_t*)Dmod_Malloc(sizeof(testfs_dp_t));
    if (!handle) return DMFSI_ERR_GENERAL;
    handle->dir_index = dir_index;
    handle->entry_pos = 0;
    handle->open = 1;
    *dp = handle;
    return DMFSI_OK;
}

// Read directory entry
// Returns DMFSI_OK and fills entry, DMFSI_ERR_NOT_FOUND at end

dmod_dmfsi_dif_api_declaration( 2.0, testfs, int, _readdir, (dmfsi_context_t ctx, void* dp, dmfsi_dir_entry_t* entry) )
{
    if (!ctx || !dp || !entry) return DMFSI_ERR_INVALID;
    testfs_dp_t* handle = (testfs_dp_t*)dp;
    testfs_context_t* fs = &ctx->ramfs;
    testfs_dir_t* dir = &fs->dirs[handle->dir_index];
    if (handle->entry_pos >= dir->file_count) return DMFSI_ERR_NOT_FOUND;
    int file_idx = dir->file_indices[handle->entry_pos];
    testfs_file_t* file = &fs->files[file_idx];
    testfs_strncpy(entry->name, file->name, sizeof(entry->name));
    entry->size = file->size;
    entry->attr = file->attr;
    entry->time = 0;
    handle->entry_pos++;
    return DMFSI_OK;
}

// Close directory

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _closedir, (dmfsi_context_t ctx, void* dp) )
{
    if (!ctx || !dp) return DMFSI_ERR_INVALID;
    testfs_dp_t* handle = (testfs_dp_t*)dp;
    handle->open = 0;
    Dmod_Free(handle);
    return DMFSI_OK;
}

// Create directory (only root supported)
dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _mkdir, (dmfsi_context_t ctx, const char* path, int mode) )
{
    if (!ctx || !path) return DMFSI_ERR_INVALID;
    testfs_context_t* fs = &ctx->ramfs;
    if (testfs_find_dir(fs, path) != -1) return DMFSI_ERR_EXISTS;
    for (int i = 0; i < TESTFS_MAX_DIRS; ++i) {
        if (!fs->dirs[i].used) {
            testfs_strncpy(fs->dirs[i].name, path, TESTFS_MAX_DIRNAME);
            fs->dirs[i].used = 1;
            fs->dirs[i].file_count = 0;
            fs->dir_count++;
            return DMFSI_OK;
        }
    }
    return DMFSI_ERR_NO_SPACE;
}

// Check if directory exists
dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _direxists, (dmfsi_context_t ctx, const char* path) )
{
    if (!ctx || !path) return DMFSI_ERR_INVALID;
    testfs_context_t* fs = &ctx->ramfs;
    for (int i = 0; i < TESTFS_MAX_DIRS; ++i) {
        if (fs->dirs[i].used && testfs_strcmp(fs->dirs[i].name, path) == 0)
            return 1;
    }
    return 0;
}

dmod_dmfsi_dif_api_declaration( 2.0, testfs, int, _stat, (dmfsi_context_t ctx, const char* path, dmfsi_stat_t* stat) )
{
    if (!ctx || !path || !stat) return DMFSI_ERR_INVALID;
    testfs_context_t* fs = &ctx->ramfs;
    for (int i = 0; i < TESTFS_MAX_FILES; ++i) {
        if (fs->files[i].used && testfs_strcmp(fs->files[i].name, path) == 0) {
            stat->size = fs->files[i].size;
            stat->attr = fs->files[i].attr;
            stat->ctime = 0;
            stat->mtime = 0;
            stat->atime = 0;
            return DMFSI_OK;
        }
    }
    return DMFSI_ERR_NOT_FOUND;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _unlink, (dmfsi_context_t ctx, const char* path) )
{
    if (!ctx || !path) return DMFSI_ERR_INVALID;
    testfs_context_t* fs = &ctx->ramfs;
    int file_index = -1;
    for (int i = 0; i < TESTFS_MAX_FILES; ++i) {
        if (fs->files[i].used && testfs_strcmp(fs->files[i].name, path) == 0) {
            file_index = i;
            break;
        }
    }
    if (file_index == -1) return DMFSI_ERR_NOT_FOUND;
    fs->files[file_index].used = 0;
    fs->file_count--;
    // Remove from directory
    for (int d = 0; d < TESTFS_MAX_DIRS; ++d) {
        if (fs->dirs[d].used) {
            for (int e = 0; e < fs->dirs[d].file_count; ++e) {
                if (fs->dirs[d].file_indices[e] == file_index) {
                    for (int k = e; k < fs->dirs[d].file_count - 1; ++k)
                        fs->dirs[d].file_indices[k] = fs->dirs[d].file_indices[k+1];
                    fs->dirs[d].file_count--;
                    break;
                }
            }
        }
    }
    return DMFSI_OK;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _rename, (dmfsi_context_t ctx, const char* oldpath, const char* newpath) )
{
    if (!ctx || !oldpath || !newpath) return DMFSI_ERR_INVALID;
    testfs_context_t* fs = &ctx->ramfs;
    for (int i = 0; i < TESTFS_MAX_FILES; ++i) {
        if (fs->files[i].used && testfs_strcmp(fs->files[i].name, oldpath) == 0) {
            testfs_strncpy(fs->files[i].name, newpath, TESTFS_MAX_FILENAME);
            return DMFSI_OK;
        }
    }
    return DMFSI_ERR_NOT_FOUND;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _chmod, (dmfsi_context_t ctx, const char* path, int mode) )
{
    if (!ctx || !path) return DMFSI_ERR_INVALID;
    testfs_context_t* fs = &ctx->ramfs;
    for (int i = 0; i < TESTFS_MAX_FILES; ++i) {
        if (fs->files[i].used && testfs_strcmp(fs->files[i].name, path) == 0) {
            fs->files[i].attr = mode;
            return DMFSI_OK;
        }
    }
    return DMFSI_ERR_NOT_FOUND;
}

dmod_dmfsi_dif_api_declaration( 1.0, testfs, int, _utime, (dmfsi_context_t ctx, const char* path, uint32_t atime, uint32_t mtime) )
{
    // Not implemented, just return OK
    return DMFSI_OK;
}
