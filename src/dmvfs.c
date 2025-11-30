#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmvfs.h"
#include <string.h>

typedef struct {
    Dmod_Context_t* fs_context;
    char* mount_point;
    dmfsi_context_t mount_context;
} mount_point_t;

typedef struct {
    mount_point_t* mount_point;
    void* fs_file;
    int pid;
} file_t;

static mount_point_t* g_mount_points = NULL;
static int g_max_mount_points = 0;
static int g_max_open_files = 0;
static void* g_mutex = NULL;
static char* g_cwd = NULL;
static char* g_pwd = NULL;
static file_t* g_open_files = NULL;

/**
 * @brief Check if DMVFS is initialized
 * @return true if initialized, false otherwise
 */
static inline bool is_initialized(void)
{
    return (g_mount_points != NULL);
}

/**
 * @brief Lock the DMVFS mutex
 * @return true on success, false on failure
 */
static inline bool lock_mutex(void)
{
    if(g_mutex == NULL)
    {
        dmvfs_reinit_mutex();
    }
    if(g_mutex != NULL)
    {
        return (Dmod_Mutex_Lock(g_mutex) == 0);
    }
    Dmod_EnterCritical();
    return true;
}

/**
 * @brief Unlock the DMVFS mutex
 */
static inline void unlock_mutex(void)
{
    if(g_mutex != NULL)
    {
        Dmod_Mutex_Unlock(g_mutex);
    }
    Dmod_ExitCritical();
}

/**
 * @brief Duplicate a string
 * @param str String to duplicate
 * @return Pointer to duplicated string, or NULL on failure
 */
static char* duplicate_string(const char* str)
{
    if(str == NULL)
    {
        return NULL;
    }

    char* dup = Dmod_Malloc(strlen(str) + 1);
    if(dup != NULL)
    {
        strcpy(dup, str);
    }
    return dup;
}

/**
 * @brief Update a string (free old and duplicate new)
 * @param old_str Old string to free
 * @param new_str New string to duplicate
 * @return Pointer to duplicated new string, or NULL on failure
 */
static char* update_string(const char* old_str, const char* new_str)
{
    if(old_str != NULL)
    {
        Dmod_Free((void*)old_str);
    }
    return duplicate_string(new_str);
}

/**
 * @brief Convert path to absolute path
 * @param path Input path
 * @return Pointer to absolute path, or NULL on failure
 */
static char* to_absolute_path(const char* path)
{
    if(path == NULL)
    {
        return NULL;
    }

    if(path[0] == '/')
    {
        return duplicate_string(path);
    }
    else
    {
        size_t cwd_len = (g_cwd != NULL) ? strlen(g_cwd) : 0;
        size_t path_len = strlen(path);
        char* abs_path = (char*)Dmod_Malloc(cwd_len + 1 + path_len + 1);
        if(abs_path != NULL)
        {
            if(cwd_len > 0)
            {
                strcpy(abs_path, g_cwd);
                abs_path[cwd_len] = '/';
                strcpy(abs_path + cwd_len + 1, path);
            }
            else
            {
                strcpy(abs_path, "/");
                strcpy(abs_path + 1, path);
            }
        }
        return abs_path;
    }
}

/**
 * @brief Find free mount point entry
 * @return Pointer to free mount point entry, or NULL if none available
 */
static mount_point_t* find_free_mount_point(void)
{
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return NULL;
    }

    for(int i = 0; i < g_max_mount_points; i++)
    {
        if(g_mount_points[i].mount_point == NULL)
        {
            return &g_mount_points[i];
        }
    }

    DMOD_LOG_ERROR("No free mount points available\n");
    return NULL;
}

/**
 * @brief Find mount point by path
 * @param mount_point Mount point path
 * @return Pointer to the mount point entry, or NULL if not found
 */
static mount_point_t* find_mount_point(const char* mount_point)
{
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return NULL;
    }

    for(int i = 0; i < g_max_mount_points; i++)
    {
        if(g_mount_points[i].mount_point != NULL &&
           strcmp(g_mount_points[i].mount_point, mount_point) == 0)
        {
            return &g_mount_points[i];
        }
    }

    DMOD_LOG_WARN("Mount point '%s' not found\n", mount_point);
    return NULL;
}

/**
 * @brief Get mount point for a given path
 * @param path File path
 * @return Pointer to the mount point entry, or NULL if not found
 */
static mount_point_t* get_mount_point_for_path(const char* path)
{
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return NULL;
    }

    for(int i = 0; i < g_max_mount_points; i++)
    {
        if(g_mount_points[i].mount_point != NULL &&
           strncmp(path, g_mount_points[i].mount_point, strlen(g_mount_points[i].mount_point)) == 0)
        {
            return &g_mount_points[i];
        }
    }

    DMOD_LOG_WARN("No mount point found for path '%s'\n", path);
    return NULL;
}

/**
 * @brief Find free file entry
 * @return Pointer to free file entry, or NULL if none available
 */
static file_t* find_free_file_entry(void)
{
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return NULL;
    }

    for(int i = 0; i < g_max_open_files; i++)
    {
        if(g_open_files[i].mount_point == NULL)
        {
            return &g_open_files[i];
        }
    }

    DMOD_LOG_ERROR("No free file entries available\n");
    return NULL;
}

/**
 * @brief Close all files of a given mount point
 * @param mp_entry Pointer to the mount point entry
 * @return true on success, false on failure
 */
static bool close_all_file_of_mount_point(mount_point_t* mp_entry)
{
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return false;
    }

    for(int i = 0; i < g_max_open_files; i++)
    {
        if(g_open_files[i].mount_point == mp_entry)
        {
            dmod_dmfsi_fclose_t close_func = (dmod_dmfsi_fclose_t)Dmod_GetDifFunction(mp_entry->fs_context, dmod_dmfsi_fclose_sig);
            if(close_func != NULL)
            {
                if(close_func(mp_entry->mount_context, g_open_files[i].fs_file) != 0)
                {
                    DMOD_LOG_ERROR("Failed to close file in mount point '%s'\n", mp_entry->mount_point);
                    return false;
                }
            }

            g_open_files[i].mount_point = NULL;
            g_open_files[i].fs_file = NULL;
        }
    }

    return true;
}

/**
 * @brief Find file system by name
 * @param fs_name Name of the file system
 * @return Pointer to the file system context, or NULL if not found
 */
static Dmod_Context_t* find_fs_by_name(const char* fs_name)
{
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return NULL;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return NULL;
    }
    Dmod_Context_t* fs_context = NULL;
    if(!Dmod_IsModuleLoaded(fs_name))
    {
        fs_context = Dmod_LoadModuleByName(fs_name);
        if(Dmod_GetDifFunction(fs_context, dmod_dmfsi_fopen_sig) == NULL)
        {
            DMOD_LOG_WARN("Module '%s' is not a valid file system\n", fs_name);
            Dmod_UnloadModule(fs_name, true);
            fs_context = NULL;
        }
    }
    else 
    {
        fs_context = Dmod_GetNextDifModule(dmod_dmfsi_fopen_sig, NULL);
        while(fs_context != NULL)
        {
            const char* moduleName = Dmod_GetName(fs_context);
            if(strcmp(moduleName, fs_name) == 0)
            {
                DMOD_LOG_VERBOSE("File system '%s' found\n", fs_name);
                unlock_mutex();
                return fs_context;
            }
            fs_context = Dmod_GetNextDifModule(dmod_dmfsi_fopen_sig, fs_context);
        }
    }
    if(fs_context)
    {
        DMOD_LOG_VERBOSE("File system '%s' found\n", fs_name);
    }
    else 
    {
        DMOD_LOG_WARN("File system '%s' not found\n", fs_name);
    }
    unlock_mutex();
    return fs_context;
}

/**
 * @brief Add mount point
 * @param mount_point Mount point path
 * @param fs_context File system context
 * @param config Configuration string for the file system (file system specific)
 * @return Pointer to the mount point entry, or NULL on failure
 */
static mount_point_t* add_mount_point(const char* mount_point, Dmod_Context_t* fs_context, const char* config)
{
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return NULL;
    }

    if(fs_context == NULL)
    {
        DMOD_LOG_ERROR("Cannot add mount point '%s': Invalid file system context\n", mount_point);
        return NULL;
    }

    const char* module_name = Dmod_GetName(fs_context);
    if(module_name == NULL)
    {
        DMOD_LOG_ERROR("Cannot add mount point '%s': Failed to get module name\n", mount_point);
        return NULL;
    }

    Dmod_BeginUsage(module_name);

    mount_point_t* free_entry = find_free_mount_point();
    if(free_entry == NULL)
    {
        return NULL;
    }

    dmod_dmfsi_init_t init_func = (dmod_dmfsi_init_t)Dmod_GetDifFunction(fs_context, dmod_dmfsi_init_sig);
    if(init_func != NULL)
    {
        free_entry->mount_context = init_func(config);
        if(free_entry->mount_context == NULL)
        {
            DMOD_LOG_ERROR("Failed to initialize mount context for mount point '%s'\n", mount_point);
            return NULL;
        }
    }

    free_entry->mount_point = Dmod_Malloc(strlen(mount_point) + 1);
    if(free_entry->mount_point == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate memory for mount point\n");
        return NULL;
    }


    strcpy(free_entry->mount_point, mount_point);
    free_entry->fs_context = fs_context;
    return free_entry;
    return NULL;
}

/**
 * @brief Remove mount point
 * @param mount_point Mount point path
 * @return true on success, false on failure
 */
static bool remove_mount_point(const char* mount_point)
{
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return false;
    }

    mount_point_t* mp_entry = find_mount_point(mount_point);
    if(mp_entry == NULL)
    {
        DMOD_LOG_ERROR("Mount point '%s' not found\n", mount_point);
        return false;
    }

    dmod_dmfsi_deinit_t deinit_func = (dmod_dmfsi_deinit_t)Dmod_GetDifFunction(mp_entry->fs_context, dmod_dmfsi_deinit_sig);
    if(deinit_func != NULL)
    {
        int result = deinit_func(mp_entry->mount_context);
        if(result != 0)
        {
            DMOD_LOG_WARN("Failed to deinitialize mount context for mount point '%s'\n", mount_point);
        }
    }

    const char* module_name = Dmod_GetName(mp_entry->fs_context);
    if(module_name == NULL)
    {
        DMOD_LOG_ERROR("Cannot remove mount point '%s': Failed to get module name\n", mount_point);
        return false;
    }

    Dmod_EndUsage(module_name);

    Dmod_Free(mp_entry->mount_point);
    mp_entry->mount_point = NULL;
    mp_entry->mount_context = NULL;
    mp_entry->fs_context = NULL;
    return true;
}

/**
 * @brief Initialize DMVFS
 * 
 * The function initializes the DMVFS with a specified maximum number of mount points.
 * It allocates memory for the mount points and creates a mutex for thread safety.
 * @param max_mount_points Maximum number of mount points
 * 
 * @return true on success, false on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, bool, _init, (int max_mount_points, int max_open_files))
{
    if (is_initialized())
    {
        DMOD_LOG_WARN("DMVFS is already initialized\n");
        return false;
    }

    if (max_mount_points <= 0)
    {
        DMOD_LOG_ERROR("Invalid maximum mount points: %d\n", max_mount_points);
        return false;
    }

    g_mount_points = (mount_point_t*)Dmod_Malloc(sizeof(mount_point_t) * max_mount_points);
    if (g_mount_points == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate memory for mount points\n");
        return false;
    }

    g_open_files = (file_t*)Dmod_Malloc(sizeof(file_t) * max_open_files);
    if (g_open_files == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate memory for open files\n");
        Dmod_Free(g_mount_points);
        g_mount_points = NULL;
        return false;
    }

    memset(g_mount_points, 0, sizeof(mount_point_t) * max_mount_points);
    g_max_mount_points = max_mount_points;
    g_max_open_files = max_open_files;

    g_mutex = Dmod_Mutex_New(true);
    if (g_mutex == NULL)
    {
        DMOD_LOG_WARN("We could not initialize mutex - working in critical-sections mode...\n");
    }

    g_cwd = duplicate_string("/");
    g_pwd = duplicate_string("/");
    if (g_cwd == NULL || g_pwd == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate memory for CWD or PWD\n");
        Dmod_Free(g_mount_points);
        g_mount_points = NULL;
        g_max_mount_points = 0;
        if (g_cwd) Dmod_Free((void*)g_cwd);
        if (g_pwd) Dmod_Free((void*)g_pwd);
        g_cwd = NULL;
        g_pwd = NULL;
        if(g_mutex != NULL)
        {
            Dmod_Mutex_Delete(g_mutex);
        }
        g_mutex = NULL;
        return false;
    }

    DMOD_LOG_INFO("== dmvfs ver " DMVFS_VERSION " ==\n");
    DMOD_LOG_INFO("DMVFS initialized with max mount points: %d\n", max_mount_points);
    return true;
}

/**
 * @brief Reinitialize DMVFS mutex
 * 
 * The function reinitializes the DMVFS mutex by destroying the old mutex
 * and creating a new one. This is useful in scenarios where the mutex
 * needs to be reset or recreated.
 * 
 * @return true on success, false on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, bool, _reinit_mutex, (void))
{
    if (!is_initialized())
    {
        DMOD_LOG_WARN("DMVFS is not initialized\n");
        return false;
    }

    if(g_mutex != NULL)
    {
        // Destroy the old mutex
        Dmod_Mutex_Delete(g_mutex);
    }

    // Create a new mutex
    g_mutex = Dmod_Mutex_New(true);
    if (g_mutex == NULL)
    {
        return false;
    }

    DMOD_LOG_INFO("DMVFS mutex reinitialized successfully\n");
    return true;
}

/**
 * @brief Deinitialize DMVFS
 * 
 * The function deinitializes the DMVFS by freeing allocated resources,
 * including the mount points and the mutex. It ensures that the DMVFS
 * is properly cleaned up and ready for reinitialization if needed.
 * 
 * @return true on success, false on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, bool, _deinit, (void))
{
    if (!is_initialized())
    {
        DMOD_LOG_WARN("DMVFS is not initialized\n");
        return false;
    }

    if (!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return false;
    }

    // Free all mount points
    for (int i = 0; i < g_max_mount_points; i++)
    {
        if (g_mount_points[i].mount_point != NULL)
        {
            remove_mount_point(g_mount_points[i].mount_point);
        }
    }

    // Free the mount points array
    Dmod_Free(g_mount_points);
    Dmod_Free(g_open_files);
    Dmod_Free(g_cwd);
    Dmod_Free(g_pwd);
    g_mount_points = NULL;
    g_max_mount_points = 0;

    // Destroy the mutex
    unlock_mutex();
    if(g_mutex != NULL)
    {
        Dmod_Mutex_Delete(g_mutex);
    }
    g_mutex = NULL;

    DMOD_LOG_INFO("DMVFS deinitialized successfully\n");
    return true;
}

/**
 * @brief Get maximum number of mount points
 * 
 * @return Maximum number of mount points
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _get_max_mount_points, (void))
{
    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }
    int result = g_max_mount_points;
    unlock_mutex();
    return result;
}

/**
 * @brief Get maximum number of open files
 * 
 * @return Maximum number of open files
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _get_max_open_files, (void))
{
    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }
    int result = g_max_open_files;
    unlock_mutex();
    return result;
}

/**
 * @brief Mount file system
 * 
 * The function mounts a file system by its name at the specified mount point.
 * It initializes the file system using its init function and registers the
 * mount point in the DMVFS.
 * 
 * @param fs_name Name of the file system to mount
 * @param mount_point Mount point path
 * @param config Configuration string for the file system (file system specific)
 * @return true on success, false on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, bool, _mount_fs, (const char* fs_name, const char* mount_point, const char* config))
{   
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return false;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return false;
    }

    Dmod_Context_t* fs_context = find_fs_by_name(fs_name);
    if(fs_context == NULL)
    {
        DMOD_LOG_ERROR("Cannot mount file system '%s': Not found\n", fs_name);
        unlock_mutex();
        return false;
    }

    mount_point_t* mp_entry = add_mount_point(mount_point, fs_context, config);
    if(mp_entry == NULL)
    {
        DMOD_LOG_ERROR("Cannot mount file system '%s'\n", fs_name);
        unlock_mutex();
        return false;
    }

    unlock_mutex();
    DMOD_LOG_INFO("File system '%s' mounted at '%s' successfully\n", fs_name, mount_point);
    return true;
}

/**
 * @brief Unmount file system
 * 
 * The function unmounts a file system at the specified mount point.
 * It deinitializes the file system using its deinit function and removes
 * the mount point from the DMVFS.
 * 
 * @param mount_point Mount point path
 * @return true on success, false on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, bool, _unmount_fs, (const char* mount_point))
{
    if(!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return false;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return false;
    }

    if(!remove_mount_point(mount_point))
    {
        DMOD_LOG_ERROR("Cannot unmount file system at mount point '%s'\n", mount_point);
        unlock_mutex();
        return false;
    }

    unlock_mutex();
    DMOD_LOG_INFO("File system at mount point '%s' unmounted successfully\n", mount_point);
    return true;
}

/**
 * @brief Open a file in the DMVFS
 * 
 * This function opens a file at the specified path with the given mode and attributes.
 * It resolves the mount point for the file, invokes the file system's open function,
 * and registers the file in the DMVFS open file table.
 * 
 * @param fp Pointer to store the file handle
 * @param path Path to the file to open
 * @param mode File open mode (e.g., read, write)
 * @param attr File attributes
 * @param pid Process ID of the caller
 * 
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _fopen, (void** fp, const char* path, int mode, int attr, int pid))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }

    if (fp == NULL || path == NULL)
    {
        DMOD_LOG_ERROR("Invalid arguments to _fopen\n");
        return -1;
    }

    if (!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    const char* abs_path = to_absolute_path(path);
    if (abs_path == NULL)
    {
        DMOD_LOG_ERROR("Failed to resolve absolute path for '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (mp_entry == NULL)
    {
        DMOD_LOG_ERROR("No mount point found for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_fopen_t fopen_func = (dmod_dmfsi_fopen_t)Dmod_GetDifFunction(mp_entry->fs_context, dmod_dmfsi_fopen_sig);
    if (fopen_func == NULL)
    {
        DMOD_LOG_ERROR("File system does not support fopen for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    void* fs_file = NULL;
    int result = fopen_func(mp_entry->mount_context, &fs_file, abs_path + strlen(mp_entry->mount_point), mode, attr);
    Dmod_Free((void*)abs_path);

    if (fs_file == NULL || result != 0)
    {
        DMOD_LOG_ERROR("Failed to open file '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    file_t* free_entry = find_free_file_entry();
    if (free_entry == NULL)
    {
        DMOD_LOG_ERROR("No free file entries available\n");
        unlock_mutex();
        return -1;
    }

    free_entry->mount_point = mp_entry;
    free_entry->fs_file = fs_file;
    free_entry->pid = pid;
    *fp = free_entry;

    unlock_mutex();
    DMOD_LOG_INFO("File '%s' opened successfully\n", path);
    return 0;
}

/**
 * @brief Close an open file in the DMVFS
 * 
 * This function closes a file that was previously opened in the DMVFS.
 * It invokes the file system's close function and removes the file
 * from the DMVFS open file table.
 * 
 * @param fp Pointer to the file handle to close
 * 
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _fclose, (void* fp))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }

    if (fp == NULL)
    {
        DMOD_LOG_ERROR("Invalid file pointer\n");
        return -1;
    }

    file_t* file_entry = (file_t*)fp;

    if (!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid file entry\n");
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_fclose_t fclose_func = (dmod_dmfsi_fclose_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_fclose_sig);

    if (fclose_func == NULL)
    {
        DMOD_LOG_ERROR("File system does not support fclose\n");
        file_entry->mount_point = NULL;
        file_entry->fs_file = NULL;
        unlock_mutex();
        return -1;
    }

    if (fclose_func(file_entry->mount_point->mount_context, file_entry->fs_file) != 0)
    {
        DMOD_LOG_ERROR("Failed to close file\n");
        file_entry->mount_point = NULL;
        file_entry->fs_file = NULL;
        unlock_mutex();
        return -1;
    }

    file_entry->mount_point = NULL;
    file_entry->fs_file = NULL;

    unlock_mutex();
    DMOD_LOG_INFO("File closed successfully\n");
    return 0;
}

/**
 * @brief Close all open files for a given process ID
 * 
 * This function iterates through the open file table and closes all files
 * associated with the specified process ID. It invokes the file system's
 * close function for each file and removes the file from the DMVFS open file table.
 * 
 * @param pid Process ID whose files should be closed
 * 
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _fclose_process, (int pid))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }

    if (!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    bool success = true;

    for (int i = 0; i < g_max_open_files; i++)
    {
        if (g_open_files[i].pid == pid && g_open_files[i].mount_point != NULL)
        {
            dmod_dmfsi_fclose_t fclose_func = (dmod_dmfsi_fclose_t)Dmod_GetDifFunction(
                g_open_files[i].mount_point->fs_context, dmod_dmfsi_fclose_sig);

            if (fclose_func != NULL)
            {
                if (fclose_func(g_open_files[i].mount_point->mount_context, g_open_files[i].fs_file) != 0)
                {
                    DMOD_LOG_ERROR("Failed to close file for process ID %d\n", pid);
                    success = false;
                }
            }

            g_open_files[i].mount_point = NULL;
            g_open_files[i].fs_file = NULL;
            g_open_files[i].pid = 0;
        }
    }

    unlock_mutex();

    if (success)
    {
        DMOD_LOG_INFO("All files for process ID %d closed successfully\n", pid);
        return 0;
    }
    else
    {
        DMOD_LOG_ERROR("Failed to close some files for process ID %d\n", pid);
        return -1;
    }
}

/**
 * @brief Read data from an open file in the DMVFS
 * 
 * This function reads data from a file that was previously opened in the DMVFS.
 * It invokes the file system's read function and updates the read_bytes parameter
 * with the number of bytes actually read.
 * 
 * @param fp Pointer to the file handle
 * @param buf Buffer to store the read data
 * @param size Number of bytes to read
 * @param read_bytes Pointer to store the number of bytes actually read
 * 
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _fread, (void* fp, void* buf, size_t size, size_t* read_bytes))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }

    if (fp == NULL || buf == NULL || size == 0)
    {
        DMOD_LOG_ERROR("Invalid arguments to _fread\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    file_t* file_entry = (file_t*)fp;

    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid file entry\n");
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_fread_t fread_func = (dmod_dmfsi_fread_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_fread_sig);

    if (fread_func == NULL)
    {
        DMOD_LOG_ERROR("File system does not support fread\n");
        unlock_mutex();
        return -1;
    }

    size_t bytes_read = 0;
    int result = fread_func(file_entry->mount_point->mount_context, file_entry->fs_file, buf, size, &bytes_read);

    if (read_bytes)
    {
        *read_bytes = bytes_read;
    }
    unlock_mutex();

    if (result != 0)
    {
        DMOD_LOG_ERROR("Failed to read from file\n");
        return -1;
    }

    DMOD_LOG_VERBOSE("Read %zu bytes from file\n", bytes_read);
    return 0;
}

/**
 * @brief Write data to an open file in the DMVFS
 *
 * This function writes data to a file that was previously opened in the DMVFS.
 * It invokes the file system's write function and updates the written_bytes parameter
 * with the number of bytes actually written.
 *
 * @param fp Pointer to the file handle
 * @param buf Buffer containing the data to write
 * @param size Number of bytes to write
 * @param written_bytes Pointer to store the number of bytes actually written
 *
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _fwrite, (void* fp, const void* buf, size_t size, size_t* written_bytes))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }

    if (fp == NULL || buf == NULL || size == 0)
    {
        DMOD_LOG_ERROR("Invalid arguments to _fwrite\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    file_t* file_entry = (file_t*)fp;

    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid file entry\n");
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_fwrite_t fwrite_func = (dmod_dmfsi_fwrite_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_fwrite_sig);

    if (fwrite_func == NULL)
    {
        DMOD_LOG_ERROR("File system does not support fwrite\n");
        unlock_mutex();
        return -1;
    }

    size_t bytes_written = 0;
    int result = fwrite_func(file_entry->mount_point->mount_context, file_entry->fs_file, buf, size, &bytes_written);

    if (written_bytes)
    {
        *written_bytes = bytes_written;
    }

    unlock_mutex();

    if (result != 0)
    {
        DMOD_LOG_ERROR("Failed to write to file\n");
        return -1;
    }

    DMOD_LOG_VERBOSE("Wrote %zu bytes to file\n", bytes_written);
    return 0;
}

/**
 * @brief Seek to a position in an open file in the DMVFS
 *
 * This function sets the file position for a file previously opened in the DMVFS.
 * It invokes the file system's lseek function.
 *
 * @param fp Pointer to the file handle
 * @param offset Offset to seek to
 * @param whence Seek mode (SEEK_SET, SEEK_CUR, SEEK_END)
 *
 * @return New offset on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _lseek, (void* fp, long offset, int whence))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }
    
    if (fp == NULL)
    {
        DMOD_LOG_ERROR("Invalid file pointer\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    file_t* file_entry = (file_t*)fp;
    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid file entry\n");
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_lseek_t lseek_func = (dmod_dmfsi_lseek_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_lseek_sig);
    
    if (lseek_func == NULL)
    {
        DMOD_LOG_ERROR("File system does not support lseek\n");
        unlock_mutex();
        return -1;
    }

    int result = lseek_func(file_entry->mount_point->mount_context, file_entry->fs_file, offset, whence);
    unlock_mutex();

    if (result < 0)
    {
        DMOD_LOG_ERROR("Failed to seek in file\n");
        return -1;
    }

    return result;
}

/**
 * @brief Get the current position in an open file in the DMVFS
 *
 * This function returns the current file position for a file previously opened in the DMVFS.
 * It invokes the file system's ftell function.
 *
 * @param fp Pointer to the file handle
 *
 * @return Current offset on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, long, _ftell, (void* fp))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }
    if (fp == NULL)
    {
        DMOD_LOG_ERROR("Invalid file pointer\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    file_t* file_entry = (file_t*)fp;
    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid file entry\n");
        unlock_mutex();
        return -1;
    }
    dmod_dmfsi_tell_t ftell_func = (dmod_dmfsi_tell_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_tell_sig);
    if (ftell_func == NULL)
    {
        DMOD_LOG_ERROR("File system does not support ftell\n");
        unlock_mutex();
        return -1;
    }
    long result = ftell_func(file_entry->mount_point->mount_context, file_entry->fs_file);
    unlock_mutex();
    
    if (result < 0)
    {
        DMOD_LOG_ERROR("Failed to get file position\n");
        return -1;
    }
    return result;
}

/**
 * @brief Check if end-of-file has been reached for a DMVFS file
 *
 * @param fp Pointer to the file handle
 * @return 1 if EOF, 0 otherwise, -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _feof, (void* fp))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }
    
    if (fp == NULL)
    {
        DMOD_LOG_ERROR("Invalid file pointer\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    file_t* file_entry = (file_t*)fp;
    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid file entry\n");
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_eof_t feof_func = (dmod_dmfsi_eof_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_eof_sig);
    
    if (feof_func == NULL)
    {
        DMOD_LOG_ERROR("File system does not support feof\n");
        unlock_mutex();
        return -1;
    }

    int result = feof_func(file_entry->mount_point->mount_context, file_entry->fs_file);
    unlock_mutex();
    return result;
}

/**
 * @brief Flush file buffers for a DMVFS file
 *
 * @param fp Pointer to the file handle
 * @return 0 on success, -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _fflush, (void* fp))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }
    
    if (fp == NULL)
    {
        DMOD_LOG_ERROR("Invalid file pointer\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    file_t* file_entry = (file_t*)fp;
    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid file entry\n");
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_fflush_t fflush_func = (dmod_dmfsi_fflush_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_fflush_sig);
    
    if (fflush_func == NULL)
    {
        DMOD_LOG_ERROR("File system does not support fflush\n");
        unlock_mutex();
        return -1;
    }

    int result = fflush_func(file_entry->mount_point->mount_context, file_entry->fs_file);
    unlock_mutex();
    return result;
}

/**
 * @brief Get error status for a DMVFS file
 *
 * @param fp Pointer to the file handle
 * @return Error code or 0 if no error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _error, (void* fp))
{
    if (!is_initialized())
    {
        DMOD_LOG_ERROR("DMVFS is not initialized\n");
        return -1;
    }
    
    if (fp == NULL)
    {
        DMOD_LOG_ERROR("Invalid file pointer\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    file_t* file_entry = (file_t*)fp;
    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid file entry\n");
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_error_t error_func = (dmod_dmfsi_error_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_error_sig);
    
    if (error_func == NULL)
    {
        DMOD_LOG_ERROR("File system does not support error\n");
        unlock_mutex();
        return -1;
    }

    int result = error_func(file_entry->mount_point->mount_context, file_entry->fs_file);
    unlock_mutex();
    return result;
}

/**
 * @brief Remove a file in DMVFS
 *
 * @param path Path to the file
 * @return 0 on success, -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _remove, (const char* path))
{
    if (!is_initialized() || path == NULL)
        return -1;
    
    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }
    
    const char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        unlock_mutex();
        return -1;
    }
    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }
    dmod_dmfsi_unlink_t remove_func = (dmod_dmfsi_unlink_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_unlink_sig);
    int result = -1;
    if (remove_func)
        result = remove_func(mp_entry->mount_context, abs_path + strlen(mp_entry->mount_point));
    Dmod_Free((void*)abs_path);
    unlock_mutex();
    return result;
}

/**
 * @brief Rename a file in DMVFS
 *
 * @param oldpath Old file path
 * @param newpath New file path
 * @return 0 on success, -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _rename, (const char* oldpath, const char* newpath))
{
    if (!is_initialized() || oldpath == NULL || newpath == NULL)
        return -1;
    
    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }
    
    const char* abs_old = to_absolute_path(oldpath);
    const char* abs_new = to_absolute_path(newpath);
    if (!abs_old || !abs_new)
    {
        if (abs_old) Dmod_Free((void*)abs_old);
        if (abs_new) Dmod_Free((void*)abs_new);
        unlock_mutex();
        return -1;
    }
    mount_point_t* mp_entry = get_mount_point_for_path(abs_old);
    if (!mp_entry)
    {
        Dmod_Free((void*)abs_old);
        Dmod_Free((void*)abs_new);
        unlock_mutex();
        return -1;
    }
    dmod_dmfsi_rename_t rename_func = (dmod_dmfsi_rename_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_rename_sig);
    int result = -1;
    if (rename_func)
        result = rename_func(mp_entry->mount_context, abs_old + strlen(mp_entry->mount_point), abs_new + strlen(mp_entry->mount_point));
    Dmod_Free((void*)abs_old);
    Dmod_Free((void*)abs_new);
    unlock_mutex();
    return result;
}

/**
 * @brief Perform an ioctl operation on a DMVFS file
 *
 * @param fp Pointer to the file handle
 * @param command Ioctl command
 * @param arg Argument for the command
 * @return 0 on success, -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _ioctl, (void* fp, int command, void* arg))
{
    if (!is_initialized() || fp == NULL)
        return -1;
    
    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }
    
    file_t* file_entry = (file_t*)fp;
    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        unlock_mutex();
        return -1;
    }
    dmod_dmfsi_ioctl_t ioctl_func = (dmod_dmfsi_ioctl_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_ioctl_sig);
    if (!ioctl_func)
    {
        unlock_mutex();
        return -1;
    }
    int result = ioctl_func(file_entry->mount_point->mount_context, file_entry->fs_file, command, arg);
    unlock_mutex();
    return result;
}

/**
 * @brief Synchronize a DMVFS file (flush to disk)
 *
 * @param fp Pointer to the file handle
 * @return 0 on success, -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _sync, (void* fp))
{
    if (!is_initialized() || fp == NULL)
        return -1;
    
    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }
    
    file_t* file_entry = (file_t*)fp;
    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        unlock_mutex();
        return -1;
    }
    dmod_dmfsi_sync_t sync_func = (dmod_dmfsi_sync_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_sync_sig);
    if (!sync_func)
    {
        unlock_mutex();
        return -1;
    }
    int result = sync_func(file_entry->mount_point->mount_context, file_entry->fs_file);
    unlock_mutex();
    return result;
}

/**
 * @brief Get file status information in DMVFS
 *
 * @param path Path to the file
 * @param stat Pointer to stat structure to fill
 * @return 0 on success, -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _stat, (const char* path, dmfsi_stat_t* stat))
{
    if (!is_initialized() || path == NULL || stat == NULL)
        return -1;
    
    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }
    
    const char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        unlock_mutex();
        return -1;
    }
    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }
    dmod_dmfsi_stat_t stat_func = (dmod_dmfsi_stat_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_stat_sig);
    int result = -1;
    if (stat_func)
        result = stat_func(mp_entry->mount_context, abs_path + strlen(mp_entry->mount_point), stat);
    Dmod_Free((void*)abs_path);
    unlock_mutex();
    return result;
}

/**
 * @brief Read a character from a DMVFS file
 *
 * @param fp Pointer to the file handle
 * @return Character read, or -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _getc, (void* fp))
{
    if (!is_initialized() || fp == NULL)
        return -1;
    
    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }
    
    file_t* file_entry = (file_t*)fp;
    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        unlock_mutex();
        return -1;
    }
    dmod_dmfsi_getc_t getc_func = (dmod_dmfsi_getc_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_getc_sig);
    if (!getc_func)
    {
        unlock_mutex();
        return -1;
    }
    int result = getc_func(file_entry->mount_point->mount_context, file_entry->fs_file);
    unlock_mutex();
    return result;
}

/**
 * @brief Write a character to a DMVFS file
 *
 * @param fp Pointer to the file handle
 * @param c Character to write
 * @return Character written, or -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _putc, (void* fp, int c))
{
    if (!is_initialized() || fp == NULL)
        return -1;
    
    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }
    
    file_t* file_entry = (file_t*)fp;
    if (file_entry->mount_point == NULL || file_entry->fs_file == NULL)
    {
        unlock_mutex();
        return -1;
    }
    dmod_dmfsi_putc_t putc_func = (dmod_dmfsi_putc_t)Dmod_GetDifFunction(
        file_entry->mount_point->fs_context, dmod_dmfsi_putc_sig);
    if (!putc_func)
    {
        unlock_mutex();
        return -1;
    }
    int result = putc_func(file_entry->mount_point->mount_context, file_entry->fs_file, c);
    unlock_mutex();
    return result;
}

/**
 * @brief Change the permissions of a file in DMVFS
 *
 * This function changes the permissions of a file at the specified path.
 * It resolves the mount point for the file and invokes the file system's chmod function.
 *
 * @param path Path to the file
 * @param mode New permissions mode
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _chmod, (const char* path, int mode))
{
    if (!is_initialized() || path == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or path is NULL\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    const char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        DMOD_LOG_ERROR("Failed to resolve absolute path for '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        DMOD_LOG_ERROR("No mount point found for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_chmod_t chmod_func = (dmod_dmfsi_chmod_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_chmod_sig);

    if (!chmod_func)
    {
        DMOD_LOG_ERROR("File system does not support chmod for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    int result = chmod_func(mp_entry->mount_context, abs_path + strlen(mp_entry->mount_point), mode);
    Dmod_Free((void*)abs_path);
    unlock_mutex();

    if (result != 0)
    {
        DMOD_LOG_ERROR("Failed to change permissions for '%s'\n", path);
        return -1;
    }

    DMOD_LOG_INFO("Permissions for '%s' changed successfully\n", path);
    return 0;
}
/**
 * @brief Update the access and modification times of a file in DMVFS
 *
 * This function updates the access and modification times of a file
 * at the specified path. It resolves the mount point for the file
 * and invokes the file system's utime function.
 *
 * @param path Path to the file
 * @param atime New access time (UNIX timestamp)
 * @param mtime New modification time (UNIX timestamp)
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _utime, (const char* path, uint32_t atime, uint32_t mtime))
{
    if (!is_initialized() || path == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or path is NULL\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    const char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        DMOD_LOG_ERROR("Failed to resolve absolute path for '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        DMOD_LOG_ERROR("No mount point found for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_utime_t utime_func = (dmod_dmfsi_utime_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_utime_sig);

    if (!utime_func)
    {
        DMOD_LOG_ERROR("File system does not support utime for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    int result = utime_func(mp_entry->mount_context, abs_path + strlen(mp_entry->mount_point), atime, mtime);
    Dmod_Free((void*)abs_path);
    unlock_mutex();

    if (result != 0)
    {
        DMOD_LOG_ERROR("Failed to update times for '%s'\n", path);
        return -1;
    }

    DMOD_LOG_INFO("Times for '%s' updated successfully\n", path);
    return 0;
}

/**
 * @brief Remove a file in DMVFS
 *
 * This function removes a file at the specified path. It resolves the
 * mount point for the file and invokes the file system's unlink function.
 *
 * @param path Path to the file
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _unlink, (const char* path))
{
    if (!is_initialized() || path == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or path is NULL\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    const char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        DMOD_LOG_ERROR("Failed to resolve absolute path for '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        DMOD_LOG_ERROR("No mount point found for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_unlink_t unlink_func = (dmod_dmfsi_unlink_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_unlink_sig);

    if (!unlink_func)
    {
        DMOD_LOG_ERROR("File system does not support unlink for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    int result = unlink_func(mp_entry->mount_context, abs_path + strlen(mp_entry->mount_point));
    Dmod_Free((void*)abs_path);
    unlock_mutex();

    if (result != 0)
    {
        DMOD_LOG_ERROR("Failed to remove file '%s'\n", path);
        return -1;
    }

    DMOD_LOG_INFO("File '%s' removed successfully\n", path);
    return 0;
}

/**
 * @brief Create a directory in DMVFS
 *
 * This function creates a directory at the specified path with the given mode.
 * It resolves the mount point for the directory and invokes the file system's mkdir function.
 *
 * @param path Path to the directory
 * @param mode Permissions mode for the new directory
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _mkdir, (const char* path, int mode))
{
    if (!is_initialized() || path == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or path is NULL\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    const char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        DMOD_LOG_ERROR("Failed to resolve absolute path for '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        DMOD_LOG_ERROR("No mount point found for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_mkdir_t mkdir_func = (dmod_dmfsi_mkdir_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_mkdir_sig);

    if (!mkdir_func)
    {
        DMOD_LOG_ERROR("File system does not support mkdir for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    int result = mkdir_func(mp_entry->mount_context, abs_path + strlen(mp_entry->mount_point), mode);
    Dmod_Free((void*)abs_path);
    unlock_mutex();

    if (result != 0)
    {
        DMOD_LOG_ERROR("Failed to create directory '%s'\n", path);
        return -1;
    }

    DMOD_LOG_INFO("Directory '%s' created successfully\n", path);
    return 0;
}
/**
 * @brief Remove a directory in DMVFS
 *
 * This function removes a directory at the specified path. It resolves the
 * mount point for the directory and invokes the file system's rmdir function.
 *
 * @param path Path to the directory
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _rmdir, (const char* path))
{
    if (!is_initialized() || path == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or path is NULL\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    const char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        DMOD_LOG_ERROR("Failed to resolve absolute path for '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        DMOD_LOG_ERROR("No mount point found for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_unlink_t rmdir_func = (dmod_dmfsi_unlink_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_unlink_sig);

    if (!rmdir_func)
    {
        DMOD_LOG_ERROR("File system does not support rmdir for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    int result = rmdir_func(mp_entry->mount_context, abs_path + strlen(mp_entry->mount_point));
    Dmod_Free((void*)abs_path);
    unlock_mutex();

    if (result != 0)
    {
        DMOD_LOG_ERROR("Failed to remove directory '%s'\n", path);
        return -1;
    }

    DMOD_LOG_INFO("Directory '%s' removed successfully\n", path);
    return 0;
}

/**
 * @brief Change the current working directory in DMVFS
 *
 * This function changes the current working directory to the specified path.
 * It resolves the mount point for the directory and verifies its existence.
 *
 * @param path Path to the new working directory
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _chdir, (const char* path))
{
    if (!is_initialized() || path == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or path is NULL\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    const char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        DMOD_LOG_ERROR("Failed to resolve absolute path for '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        DMOD_LOG_ERROR("No mount point found for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_direxists_t direxists_func = (dmod_dmfsi_direxists_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_direxists_sig);

    if (!direxists_func || !direxists_func(mp_entry->mount_context, abs_path + strlen(mp_entry->mount_point)))
    {
        DMOD_LOG_ERROR("Directory '%s' does not exist\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    g_cwd = update_string(g_cwd, abs_path);
    Dmod_Free((void*)abs_path);

    if (!g_cwd)
    {
        DMOD_LOG_ERROR("Failed to update current working directory\n");
        unlock_mutex();
        return -1;
    }

    DMOD_LOG_INFO("Current working directory changed to '%s'\n", g_cwd);
    unlock_mutex();
    return 0;
}

/**
 * @brief Open a directory in DMVFS
 *
 * This function opens a directory at the specified path and returns a directory
 * handle that can be used for reading directory entries.
 *
 * @param dp Pointer to store the directory handle
 * @param path Path to the directory
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _opendir, (void** dp, const char* path))
{
    if (!is_initialized() || dp == NULL || path == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or invalid arguments to _opendir\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    const char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        DMOD_LOG_ERROR("Failed to resolve absolute path for '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        DMOD_LOG_ERROR("No mount point found for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_opendir_t opendir_func = (dmod_dmfsi_opendir_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_opendir_sig);

    if (!opendir_func)
    {
        DMOD_LOG_ERROR("File system does not support opendir for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    void* dir_handle = NULL;
    int result = opendir_func(mp_entry->mount_context, &dir_handle, abs_path + strlen(mp_entry->mount_point));
    Dmod_Free((void*)abs_path);

    if (result != 0 || dir_handle == NULL)
    {
        DMOD_LOG_ERROR("Failed to open directory '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    file_t* free_entry = find_free_file_entry();
    if (free_entry == NULL) {
        DMOD_LOG_ERROR("No free file entries available for directory\n");
        unlock_mutex();
        return -1;
    }
    free_entry->mount_point = mp_entry;
    free_entry->fs_file = dir_handle;
    free_entry->pid = 0; 

    *dp = free_entry;
    DMOD_LOG_INFO("Directory '%s' opened successfully\n", path);
    unlock_mutex();
    return 0;
}
/**
 * @brief Read the next directory entry in DMVFS
 *
 * This function reads the next directory entry from an open directory handle.
 * It invokes the file system's readdir function to retrieve the directory entry.
 *
 * @param dp Pointer to the directory handle
 * @param entry Pointer to a structure to store the directory entry
 * @return 0 on success, -1 on failure or end of directory
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _readdir, (void* dp, dmfsi_dir_entry_t* entry))
{
    if (!is_initialized() || dp == NULL || entry == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or invalid arguments to _readdir\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    file_t* dir_entry = (file_t*)dp;

    if (dir_entry->mount_point == NULL || dir_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid directory handle\n");
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_readdir_t readdir_func = (dmod_dmfsi_readdir_t)Dmod_GetDifFunction(
        dir_entry->mount_point->fs_context, dmod_dmfsi_readdir_sig);

    if (!readdir_func)
    {
        DMOD_LOG_ERROR("File system does not support readdir\n");
        unlock_mutex();
        return -1;
    }

    int result = readdir_func(dir_entry->mount_point->mount_context, dir_entry->fs_file, entry);
    unlock_mutex();

    if (result != 0)
    {
        DMOD_LOG_VERBOSE("End of directory or error reading directory\n");
        return -1;
    }

    return 0;
}
/**
 * @brief Close an open directory in DMVFS
 *
 * This function closes a directory that was previously opened in DMVFS.
 * It invokes the file system's closedir function and releases the directory handle.
 *
 * @param dp Pointer to the directory handle
 * @return 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _closedir, (void* dp))
{
    if (!is_initialized() || dp == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or invalid directory handle\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    file_t* dir_entry = (file_t*)dp;

    if (dir_entry->mount_point == NULL || dir_entry->fs_file == NULL)
    {
        DMOD_LOG_ERROR("Invalid directory handle\n");
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_closedir_t closedir_func = (dmod_dmfsi_closedir_t)Dmod_GetDifFunction(
        dir_entry->mount_point->fs_context, dmod_dmfsi_closedir_sig);

    if (!closedir_func)
    {
        DMOD_LOG_ERROR("File system does not support closedir\n");
        unlock_mutex();
        return -1;
    }

    int result = closedir_func(dir_entry->mount_point->mount_context, dir_entry->fs_file);

    if (result != 0)
    {
        DMOD_LOG_ERROR("Failed to close directory\n");
        unlock_mutex();
        return -1;
    }

    dir_entry->mount_point = NULL;
    dir_entry->fs_file = NULL;

    DMOD_LOG_INFO("Directory closed successfully\n");
    unlock_mutex();
    return 0;
}
/**
 * @brief Check if a directory exists in DMVFS
 *
 * This function checks if a directory exists at the specified path.
 * It resolves the mount point for the directory and invokes the file system's direxists function.
 *
 * @param path Path to the directory
 * @return 1 if the directory exists, 0 if it does not exist, -1 on error
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _direxists, (const char* path))
{
    if (!is_initialized() || path == NULL)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or path is NULL\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    char* abs_path = to_absolute_path(path);
    if (!abs_path)
    {
        DMOD_LOG_ERROR("Failed to resolve absolute path for '%s'\n", path);
        unlock_mutex();
        return -1;
    }

    mount_point_t* mp_entry = get_mount_point_for_path(abs_path);
    if (!mp_entry)
    {
        DMOD_LOG_ERROR("No mount point found for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    dmod_dmfsi_direxists_t direxists_func = (dmod_dmfsi_direxists_t)Dmod_GetDifFunction(
        mp_entry->fs_context, dmod_dmfsi_direxists_sig);

    if (!direxists_func)
    {
        DMOD_LOG_ERROR("File system does not support direxists for path '%s'\n", abs_path);
        Dmod_Free((void*)abs_path);
        unlock_mutex();
        return -1;
    }

    int result = direxists_func(mp_entry->mount_context, abs_path + strlen(mp_entry->mount_point));
    Dmod_Free((void*)abs_path);
    unlock_mutex();

    return result;
}

/**
 * @brief Get the current working directory in DMVFS
 *
 * This function retrieves the current working directory and copies it to the provided buffer.
 *
 * @param buffer Buffer to store the current working directory
 * @param size Size of the buffer
 * @return 0 on success, -1 on failure (e.g., buffer too small or DMVFS not initialized)
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _getcwd, (char* buffer, size_t size))
{
    if (!is_initialized() || buffer == NULL || size == 0)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or invalid arguments to _getcwd\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    if (strlen(g_cwd) + 1 > size)
    {
        DMOD_LOG_ERROR("Buffer too small for current working directory\n");
        unlock_mutex();
        return -1;
    }

    strcpy(buffer, g_cwd);
    unlock_mutex();
    return 0;
}

/**
 * @brief Get the current process working directory in DMVFS
 *
 * This function retrieves the current process working directory (PWD) and copies it to the provided buffer.
 *
 * @param buffer Buffer to store the current process working directory
 * @param size Size of the buffer
 * @return 0 on success, -1 on failure (e.g., buffer too small or DMVFS not initialized)
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _getpwd, (char* buffer, size_t size))
{
    if (!is_initialized() || buffer == NULL || size == 0)
    {
        DMOD_LOG_ERROR("DMVFS is not initialized or invalid arguments to _getpwd\n");
        return -1;
    }

    if(!lock_mutex())
    {
        DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
        return -1;
    }

    if (strlen(g_pwd) + 1 > size)
    {
        DMOD_LOG_ERROR("Buffer too small for process working directory\n");
        unlock_mutex();
        return -1;
    }

    strcpy(buffer, g_pwd);
    unlock_mutex();
    return 0;
}

/**
 * @brief Convert a relative path to an absolute path
 * 
 * This function converts a given relative path to an absolute path
 * based on the current working directory (CWD). If the input path
 * is already absolute, it simply copies the path to the output buffer.
 * 
 * @param path Input path (relative or absolute)
 * @param abs_path Buffer to store the resulting absolute path
 * @param size Size of the abs_path buffer
 * 
 * @return 0 on success, -1 on failure (e.g., buffer too small or invalid input)
 */
DMOD_INPUT_API_DECLARATION(dmvfs, 1.0, int, _toabs, (const char* path, char* abs_path, size_t size))
{
    if (path == NULL || abs_path == NULL || size == 0)
    {
        DMOD_LOG_ERROR("Invalid arguments to _toabs\n");
        return -1;
    }

    if (path[0] == '/')
    {
        // Path is already absolute
        if (strlen(path) + 1 > size)
        {
            DMOD_LOG_ERROR("Buffer too small for absolute path\n");
            return -1;
        }
        strcpy(abs_path, path);
    }
    else
    {
        // Path is relative, prepend the current working directory
        if (!is_initialized())
        {
            DMOD_LOG_ERROR("DMVFS is not initialized\n");
            return -1;
        }

        if(!lock_mutex())
        {
            DMOD_LOG_ERROR("Failed to lock DMVFS mutex\n");
            return -1;
        }

        if (g_cwd == NULL)
        {
            DMOD_LOG_ERROR("CWD is NULL\n");
            unlock_mutex();
            return -1;
        }

        size_t cwd_len = strlen(g_cwd);
        size_t path_len = strlen(path);

        if (cwd_len + 1 + path_len + 1 > size)
        {
            DMOD_LOG_ERROR("Buffer too small for absolute path\n");
            unlock_mutex();
            return -1;
        }

        strncpy(abs_path, g_cwd, size - 1);
        abs_path[size - 1] = '\0';
        strncat(abs_path, "/", size - strlen(abs_path) - 1);
        strncat(abs_path, path, size - strlen(abs_path) - 1);
        unlock_mutex();
    }

    return 0;
}