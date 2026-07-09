#ifndef DMVFS_H
#define DMVFS_H

#include "dmod.h"
#include "dmfsi.h"

// Module name reported to Dmod_MallocEx/Dmod_FreeEx for allocations tied to dmvfs
#define DMVFS_ALLOCATOR_NAME    "dmvfs"

DMOD_BUILTIN_API( dmvfs, 1.0, bool, _init, (int max_mount_points, int max_open_files) );
DMOD_BUILTIN_API( dmvfs, 1.0, bool, _reinit_mutex, (void) );
DMOD_BUILTIN_API( dmvfs, 1.0, bool, _deinit, (void) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _get_max_mount_points, (void) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _get_max_open_files, (void) );

DMOD_BUILTIN_API( dmvfs, 1.0, bool, _mount_fs, (const char* fs_name, const char* mount_point, const char* config) );
DMOD_BUILTIN_API( dmvfs, 1.0, bool, _unmount_fs, (const char* mount_point) );

DMOD_BUILTIN_API( dmvfs, 1.0, int, _fopen, (void** fp, const char* path, int mode, int attr, int pid) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _fclose, (void* fp) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _fclose_process, (int pid) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _fread, (void* fp, void* buf, size_t size, size_t* read_bytes) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _fwrite, (void* fp, const void* buf, size_t size, size_t* written_bytes) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _lseek, (void* fp, long offset, int whence) );
DMOD_BUILTIN_API( dmvfs, 1.0, long, _ftell, (void* fp) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _feof, (void* fp) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _fflush, (void* fp) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _error, (void* fp) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _remove, (const char* path) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _rename, (const char* oldpath, const char* newpath) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _ioctl, (void* fp, int command, void* arg) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _sync, (void* fp) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _stat, (const char* path, dmfsi_stat_t* stat) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _getc, (void* fp) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _putc, (void* fp, int c) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _chmod, (const char* path, int mode) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _utime, (const char* path, uint32_t atime, uint32_t mtime) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _unlink, (const char* path) );


// Directory operations
DMOD_BUILTIN_API( dmvfs, 1.0, int, _mkdir, (const char* path, int mode) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _rmdir, (const char* path) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _chdir, (const char* path) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _opendir, (void** dp, const char* path) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _readdir, (void* dp, dmfsi_dir_entry_t* entry) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _closedir, (void* dp) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _direxists, (const char* path) );

// Current working directory
DMOD_BUILTIN_API( dmvfs, 1.0, int, _getcwd, (char* buffer, size_t size) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _getpwd, (char* buffer, size_t size) );
DMOD_BUILTIN_API( dmvfs, 1.0, int, _setpwd, (const char* path) );

DMOD_BUILTIN_API( dmvfs, 1.0, int, _toabs, (const char* path, char* abs_path, size_t size) );

#endif // DMVFS_H