/**
 * @file dmod_sal.c
 * @brief DMOD SAL (System Abstraction Layer) implementations for file operations
 * 
 * This file provides implementations for the DMOD SAL file and directory
 * operations using the dmvfs (DMOD Virtual File System) library.
 * 
 * These implementations override the weak prototypes defined in dmod_sal.h
 * to provide file system operations through dmvfs.
 */

#ifndef DMVFS_DONT_IMPLEMENT_DMOD_SAL

#include "dmvfs.h"
#include "dmfsi.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

/**
 * @brief Open a file
 * 
 * @param Path Path to the file
 * @param Mode Mode string ("r", "w", "a", "r+", "w+", "a+")
 * @return void* File handle, or NULL on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, void*, _FileOpen, (const char* Path, const char* Mode))
{
    if (Path == NULL || Mode == NULL)
    {
        return NULL;
    }
    
    int flags = 0;
    
    // Parse mode string to flags
    if (strchr(Mode, 'r') != NULL)
    {
        if (strchr(Mode, '+') != NULL)
        {
            flags = DMFSI_O_RDWR;
        }
        else
        {
            flags = DMFSI_O_RDONLY;
        }
    }
    else if (strchr(Mode, 'w') != NULL)
    {
        if (strchr(Mode, '+') != NULL)
        {
            flags = DMFSI_O_RDWR | DMFSI_O_CREAT | DMFSI_O_TRUNC;
        }
        else
        {
            flags = DMFSI_O_WRONLY | DMFSI_O_CREAT | DMFSI_O_TRUNC;
        }
    }
    else if (strchr(Mode, 'a') != NULL)
    {
        if (strchr(Mode, '+') != NULL)
        {
            flags = DMFSI_O_RDWR | DMFSI_O_CREAT | DMFSI_O_APPEND;
        }
        else
        {
            flags = DMFSI_O_WRONLY | DMFSI_O_CREAT | DMFSI_O_APPEND;
        }
    }
    else
    {
        // Default to read-only if no valid mode character
        flags = DMFSI_O_RDONLY;
    }
    
    void* fp = NULL;
    int ret = dmvfs_fopen(&fp, Path, flags, 0, 0);
    
    if (ret != 0)
    {
        return NULL;
    }
    
    return fp;
}

/**
 * @brief Read data from a file
 * 
 * @param Buffer Buffer to store read data
 * @param Size Size of each element
 * @param Count Number of elements to read
 * @param File File handle
 * @return size_t Number of elements read
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, size_t, _FileRead, (void* Buffer, size_t Size, size_t Count, void* File))
{
    if (Buffer == NULL || File == NULL || Size == 0 || Count == 0)
    {
        return 0;
    }

    // Check for overflow before multiplication
    if (Count > SIZE_MAX / Size)
    {
        return 0;
    }

    size_t total_size = Size * Count;

    // DMOD_STDIN/OUT/ERR/LOG are virtual stream handles, not dmvfs file_t*
    // pointers. Resolve them first: if nothing is bound to the stream for
    // the current process, fall back to the raw kernel I/O used before any
    // real file is attached (this is also what the dmod default weak
    // Dmod_FileRead does).
    void* resolvedFile = Dmod_LockStdio(File);
    if (resolvedFile == NULL)
    {
        size_t read = Dmod_ReadKernel(Buffer, total_size);
        return read / Size;
    }

    size_t read_bytes = 0;
    int ret = dmvfs_fread(resolvedFile, Buffer, total_size, &read_bytes);
    Dmod_UnlockStdio(File);

    if (ret != 0)
    {
        return 0;
    }

    return read_bytes / Size;
}

/**
 * @brief Write data to a file
 * 
 * @param Buffer Buffer containing data to write
 * @param Size Size of each element
 * @param Count Number of elements to write
 * @param File File handle
 * @return size_t Number of elements written
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, size_t, _FileWrite, (const void* Buffer, size_t Size, size_t Count, void* File))
{
    if (Buffer == NULL || File == NULL || Size == 0 || Count == 0)
    {
        return 0;
    }

    // Check for overflow before multiplication
    if (Count > SIZE_MAX / Size)
    {
        return 0;
    }

    size_t total_size = Size * Count;

    // DMOD_STDIN/OUT/ERR/LOG are virtual stream handles, not dmvfs file_t*
    // pointers. Resolve them first: if nothing is bound to the stream for
    // the current process, fall back to the raw kernel I/O used before any
    // real file is attached (this is also what the dmod default weak
    // Dmod_FileWrite does). Without this, Dmod_Printf(DMOD_STDOUT) before
    // dmvfs is initialized recurses forever: dmvfs_fwrite() sees "not
    // initialized", logs an error via Dmod_Printf, which calls back into
    // this same function.
    void* resolvedFile = Dmod_LockStdio(File);
    if (resolvedFile == NULL)
    {
        size_t written = Dmod_WriteKernel(Buffer, total_size);
        return written / Size;
    }

    size_t written_bytes = 0;
    int ret = dmvfs_fwrite(resolvedFile, Buffer, total_size, &written_bytes);
    Dmod_UnlockStdio(File);

    if (ret != 0)
    {
        return 0;
    }

    return written_bytes / Size;
}

/**
 * @brief Seek to a position in a file
 * 
 * @param File File handle
 * @param Offset Offset to seek
 * @param Origin Origin for seek (DMOD_SEEK_SET, DMOD_SEEK_CUR, DMOD_SEEK_END)
 * @return int 0 on success, non-zero on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, int, _FileSeek, (void* File, long Offset, int Origin))
{
    if (File == NULL)
    {
        return -1;
    }
    
    // Map DMOD_SEEK_* to DMFSI_SEEK_*
    int whence;
    switch (Origin)
    {
        case 0: // DMOD_SEEK_SET
            whence = DMFSI_SEEK_SET;
            break;
        case 1: // DMOD_SEEK_CUR
            whence = DMFSI_SEEK_CUR;
            break;
        case 2: // DMOD_SEEK_END
            whence = DMFSI_SEEK_END;
            break;
        default:
            return -1;
    }
    
    int result = dmvfs_lseek(File, Offset, whence);
    
    return (result >= 0) ? 0 : -1;
}

/**
 * @brief Get current position in a file
 * 
 * @param File File handle
 * @return size_t Current position, or 0 on error
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, size_t, _FileTell, (void* File))
{
    if (File == NULL)
    {
        return 0;
    }
    
    long pos = dmvfs_ftell(File);
    
    return (pos >= 0) ? (size_t)pos : 0;
}

/**
 * @brief Get the size of a file
 * 
 * @param File File handle
 * @return size_t File size, or 0 on error
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, size_t, _FileSize, (void* File))
{
    if (File == NULL)
    {
        return 0;
    }
    
    // Save current position
    long current_pos = dmvfs_ftell(File);
    if (current_pos < 0)
    {
        return 0;
    }
    
    // Seek to end
    if (dmvfs_lseek(File, 0, DMFSI_SEEK_END) < 0)
    {
        return 0;
    }
    
    // Get position (which is the file size)
    long size = dmvfs_ftell(File);
    
    // Restore original position - if this fails, we have a problem
    // but we should still return the size we got
    if (dmvfs_lseek(File, current_pos, DMFSI_SEEK_SET) < 0)
    {
        // Log the issue but still return the size
        DMOD_LOG_WARN("Failed to restore file position after getting size\n");
    }
    
    return (size >= 0) ? (size_t)size : 0;
}

/**
 * @brief Close a file
 * 
 * @param File File handle
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, void, _FileClose, (void* File))
{
    if (File != NULL)
    {
        dmvfs_fclose(File);
    }
}

/**
 * @brief Issue a driver-specific ioctl on a file
 *
 * DMOD_STDIN/OUT/ERR/LOG are virtual stream handles, not dmvfs file_t*
 * pointers, so resolve them first exactly like Dmod_FileRead/Dmod_FileWrite
 * do. If nothing is bound to the stream for the current process (raw kernel
 * I/O fallback), there is no driver to forward the ioctl to.
 *
 * @param File File handle, or one of DMOD_STDIN/DMOD_STDOUT/DMOD_STDERR/DMOD_STDLOG
 * @param Command Driver-specific ioctl command
 * @param Arg Command-specific argument
 * @return 0 on success, negative error code on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, int, _Ioctl, (void* File, int Command, void* Arg))
{
    if (File == NULL)
    {
        return -EINVAL;
    }

    void* resolvedFile = Dmod_LockStdio(File);
    if (resolvedFile == NULL)
    {
        // Nothing bound (raw kernel I/O fallback) - no driver to forward to.
        return -ENOTTY;
    }

    int result = dmvfs_ioctl(resolvedFile, Command, Arg);
    Dmod_UnlockStdio(File);

    return result;
}

/**
 * @brief Check if a file exists/is available
 * 
 * @param Path Path to the file
 * @return bool true if file exists, false otherwise
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, bool, _FileAvailable, (const char* Path))
{
    if (Path == NULL)
    {
        return false;
    }
    
    dmfsi_stat_t stat;
    int ret = dmvfs_stat(Path, &stat);
    
    return (ret == 0);
}

/**
 * @brief Open a directory
 * 
 * @param Path Path to the directory
 * @return void* Directory handle, or NULL on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, void*, _OpenDir, (const char* Path))
{
    if (Path == NULL)
    {
        return NULL;
    }
    
    void* dp = NULL;
    int ret = dmvfs_opendir(&dp, Path);
    
    if (ret != 0)
    {
        return NULL;
    }
    
    return dp;
}

/**
 * @brief Directory entry storage for ReadDir and ReadDirEx
 * 
 * We need to store the last directory entry because the DMOD SAL API
 * returns a const char* / const Dmod_DirEntry_t* which must persist until
 * the next call.
 * 
 * @note Thread safety: These static buffers are NOT thread-safe. Concurrent
 *       calls to _ReadDir or _ReadDirEx from different threads may result in
 *       race conditions. The DMOD SAL API design requires this pattern.
 *       Callers should ensure thread-safe access if needed.
 */
static char g_last_dir_entry_name[256] = {0};
static Dmod_DirEntry_t g_last_dir_entry = {0};

/**
 * @brief Read the next directory entry
 * 
 * @param Dir Directory handle
 * @return const char* Name of the next entry, or NULL if no more entries
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, const char*, _ReadDir, (void* Dir))
{
    if (Dir == NULL)
    {
        return NULL;
    }
    
    dmfsi_dir_entry_t entry;
    int ret = dmvfs_readdir(Dir, &entry);
    
    if (ret != 0)
    {
        return NULL;
    }
    
    // Copy entry name to persistent storage
    strncpy(g_last_dir_entry_name, entry.name, sizeof(g_last_dir_entry_name) - 1);
    g_last_dir_entry_name[sizeof(g_last_dir_entry_name) - 1] = '\0';
    
    return g_last_dir_entry_name;
}

/**
 * @brief Read the next directory entry with extended information
 * 
 * @param Dir Directory handle
 * @return const Dmod_DirEntry_t* Pointer to entry info, or NULL if no more entries
 * 
 * @note The returned pointer points to static storage that may be overwritten by subsequent calls.
 *       Not thread-safe: use separate directory handles per thread.
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, const Dmod_DirEntry_t*, _ReadDirEx, (void* Dir))
{
    if (Dir == NULL)
    {
        return NULL;
    }

    dmfsi_dir_entry_t entry;
    int ret = dmvfs_readdir(Dir, &entry);

    if (ret != 0)
    {
        return NULL;
    }

    // Copy entry name to persistent storage (reuse the existing buffer)
    strncpy(g_last_dir_entry_name, entry.name, sizeof(g_last_dir_entry_name) - 1);
    g_last_dir_entry_name[sizeof(g_last_dir_entry_name) - 1] = '\0';

    // Map dmfsi attr flags to Dmod_DirEntryType_t
    g_last_dir_entry.name = g_last_dir_entry_name;
    if (entry.attr & DMFSI_ATTR_DIRECTORY)
    {
        g_last_dir_entry.type = Dmod_DirEntryType_Dir;
    }
    else
    {
        g_last_dir_entry.type = Dmod_DirEntryType_File;
    }

    return &g_last_dir_entry;
}

/**
 * @brief Close a directory
 * 
 * @param Dir Directory handle
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, void, _CloseDir, (void* Dir))
{
    if (Dir != NULL)
    {
        dmvfs_closedir(Dir);
    }
}

/**
 * @brief Create a directory
 * 
 * @param Path Path to the directory
 * @param Mode Directory permissions mode
 * @return int 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, int, _MakeDir, (const char* Path, int Mode))
{
    if (Path == NULL)
    {
        return -1;
    }
    
    int ret = dmvfs_mkdir(Path, Mode);
    
    return (ret == 0) ? 0 : -1;
}

/**
 * @brief Check file access permissions
 * 
 * @param Path Path to the file
 * @param Mode Access mode to check (DMOD_R_OK, DMOD_W_OK, DMOD_X_OK, DMOD_F_OK)
 * @return int 0 if access is permitted, -1 otherwise
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, int, _Access, (const char* Path, int Mode))
{
    if (Path == NULL)
    {
        return -1;
    }
    
    // For existence check (F_OK = 0), just check if file exists
    dmfsi_stat_t stat;
    int ret = dmvfs_stat(Path, &stat);
    
    if (ret != 0)
    {
        // File doesn't exist, also check if it's a directory
        ret = dmvfs_direxists(Path);
        if (ret != 1)
        {
            return -1;
        }
    }
    
    // For other access modes, we'd need proper permission checking
    // For now, if file exists and Mode is just F_OK (0), return success
    // For R_OK, W_OK, X_OK, we assume access is granted if file exists
    return 0;
}

/**
 * @brief Read a line from a file
 * 
 * @param Buffer Buffer to store the line
 * @param Size Size of the buffer
 * @param File File handle
 * @return char* Buffer on success, NULL on failure or EOF
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, char*, _FileReadLine, (char* Buffer, int Size, void* File))
{
    if (Buffer == NULL || Size <= 0 || File == NULL)
    {
        return NULL;
    }
    
    int i = 0;
    int ch;
    
    while (i < Size - 1)
    {
        ch = dmvfs_getc(File);
        
        if (ch < 0)
        {
            // EOF or error
            if (i == 0)
            {
                return NULL;
            }
            break;
        }
        
        Buffer[i++] = (char)ch;
        
        if (ch == '\n')
        {
            break;
        }
    }
    
    Buffer[i] = '\0';
    
    return Buffer;
}

/**
 * @brief Change current working directory
 * 
 * @param Path Path to the new working directory
 * @return int 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, int, _ChDir, (const char* Path))
{
    if (Path == NULL)
    {
        return -1;
    }
    
    int ret = dmvfs_chdir(Path);
    
    return (ret == 0) ? 0 : -1;
}

/**
 * @brief Get current working directory
 * 
 * @param Buffer Buffer to store the path
 * @param Size Size of the buffer
 * @return char* Buffer on success, NULL on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, char*, _GetCwd, (char* Buffer, size_t Size))
{
    if (Buffer == NULL || Size == 0)
    {
        return NULL;
    }
    
    int ret = dmvfs_getcwd(Buffer, Size);
    
    return (ret == 0) ? Buffer : NULL;
}

/**
 * @brief Get process working directory
 * 
 * @param Buffer Buffer to store the path
 * @param Size Size of the buffer
 * @return char* Buffer on success, NULL on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, char*, _GetPwd, (char* Buffer, size_t Size))
{
    if (Buffer == NULL || Size == 0)
    {
        return NULL;
    }
    
    int ret = dmvfs_getpwd(Buffer, Size);
    
    return (ret == 0) ? Buffer : NULL;
}

/**
 * @brief Set process working directory
 * 
 * @param Path Path to set as the process working directory
 * @return int 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, int, _SetPwd, (const char* Path))
{
    if (Path == NULL)
    {
        return -1;
    }
    
    return dmvfs_setpwd(Path);
}

/**
 * @brief Rename a file or directory
 * 
 * @param OldPath Current path
 * @param NewPath New path
 * @return int 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, int, _Rename, (const char* OldPath, const char* NewPath))
{
    if (OldPath == NULL || NewPath == NULL)
    {
        return -1;
    }
    
    int ret = dmvfs_rename(OldPath, NewPath);
    
    return (ret == 0) ? 0 : -1;
}

/**
 * @brief Remove a directory
 * 
 * @param Path Path to the directory
 * @return int 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, int, _RemoveDir, (const char* Path))
{
    if (Path == NULL)
    {
        return -1;
    }
    
    int ret = dmvfs_rmdir(Path);
    
    return (ret == 0) ? 0 : -1;
}

/**
 * @brief Remove a file
 * 
 * @param Path Path to the file
 * @return int 0 on success, -1 on failure
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, int, _FileRemove, (const char* Path))
{
    if (Path == NULL)
    {
        return -1;
    }
    
    int ret = dmvfs_unlink(Path);
    
    return (ret == 0) ? 0 : -1;
}

/**
 * @brief Get repository directory
 * 
 * @return const char* Repository directory path
 * 
 * @note This function returns the current working directory as a fallback
 *       since dmvfs doesn't have a concept of a repository directory.
 *       The caller should use environment variables or configuration 
 *       to determine the repository path if needed.
 * 
 * @note Thread safety: The static buffer initialization is NOT atomic.
 *       First-time initialization from multiple threads may result in
 *       race conditions. However, once initialized, the buffer content
 *       is immutable and safe to read.
 */
DMOD_INPUT_API_DECLARATION(Dmod, 1.0, const char*, _GetRepoDir, (void))
{
    // Get the current working directory as a fallback
    // Using simple static initialization - not thread-safe during first init
    static char repo_dir[256] = {0};
    static bool initialized = false;
    
    if (!initialized)
    {
        if (dmvfs_getcwd(repo_dir, sizeof(repo_dir)) != 0)
        {
            // If we can't get CWD, return root
            strcpy(repo_dir, "/");
        }
        initialized = true;
    }
    
    return repo_dir;
}

#endif /* DMVFS_DONT_IMPLEMENT_DMOD_SAL */
