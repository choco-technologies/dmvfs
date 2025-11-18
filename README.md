# DMVFS - DMOD Virtual File System


DMVFS is a Virtual File System (VFS) layer for DMOD-based embedded systems. It provides a unified interface for managing multiple file systems simultaneously, allowing applications to work with different file system implementations through a single, consistent API.

## Overview

DMVFS acts as a virtual file system layer that sits on top of various file system implementations (such as FatFS, RamFS, FlashFS, etc.). It allows multiple file systems to be mounted at different mount points in a unified directory tree, similar to how traditional operating systems handle multiple partitions and storage devices.

The key advantage of DMVFS is that it leverages the **DMOD (Dynamic Modules)** system to dynamically load file system implementations at runtime, providing flexibility and modularity for embedded applications.

## Key Features

- **Multi-Mount Point Support**: Mount multiple file systems at different locations in a unified directory tree
- **Dynamic Module Loading**: Load file system implementations dynamically at runtime using DMOD
- **POSIX-like API**: Familiar file operations (open, read, write, seek, etc.)
- **Process-Based File Management**: Track open files per process ID
- **Thread-Safe Operations**: Built-in mutex protection for concurrent access
- **Path Resolution**: Automatic conversion between relative and absolute paths
- **Comprehensive File Operations**: Support for files, directories, and metadata operations
- **Modular Architecture**: Clean separation between VFS layer and file system implementations

## Architecture

DMVFS is built on top of two key DMOD components:

### 1. DMOD (Dynamic Modules)
The foundation that provides:
- Dynamic loading and unloading of modules at runtime
- Inter-module communication through a common API
- Resource management and dependency handling
- Module lifecycle management

[Learn more about DMOD](https://github.com/choco-technologies/dmod)

### 2. DMFSI (DMOD File System Interface)
A standardized interface specification that:
- Defines a comprehensive set of file system operations
- Provides POSIX-like semantics for file and directory operations
- Enables different file system implementations to work interchangeably
- Ensures compatibility between VFS and underlying file systems

[Learn more about DMFSI](https://github.com/choco-technologies/dmfsi)

### How It Works

```
┌─────────────────────────────────────────────────┐
│           Application Layer                      │
│   (Uses DMVFS API for file operations)         │
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│              DMVFS (This Project)                │
│  • Path resolution & mount point management     │
│  • File descriptor management                   │
│  • Process-based file tracking                  │
│  • Thread-safe operations                       │
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│         DMFSI (File System Interface)           │
│  • Standardized FS operation signatures        │
│  • POSIX-like API definition                    │
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│       File System Implementations                │
│  (FatFS, RamFS, FlashFS, etc. - loaded via DMOD)│
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│           Storage Hardware                       │
│  (SD Card, Flash Memory, RAM, etc.)             │
└─────────────────────────────────────────────────┘
```

## Prerequisites

- **Compiler**: GCC or compatible C compiler
- **Build System**: CMake 3.10 or higher
- **Dependencies**:
  - DMOD library (automatically fetched via CMake)
  - DMFSI interface (automatically fetched via CMake)
- **Dynamic Memory**: Your system must support dynamic memory allocation

## Building

### Using CMake (Recommended)

DMVFS uses CMake with FetchContent to automatically download and build dependencies:

```bash
# Create build directory
mkdir build
cd build

# Configure the project
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build the library
cmake --build .
```

The build process will:
1. Fetch and build the DMOD library from GitHub
2. Fetch and build the DMFSI interface from GitHub
3. Build the DMVFS static library (`libdmvfs.a`)
4. Build the test suite

### Build Outputs

After building, you'll find:
- `build/libdmvfs.a` - The DMVFS static library
- `build/tests/fs_tester` - Test executable for file system validation

## Usage

### Basic Example

Here's a simple example of using DMVFS in your application:

```c
#include "dmvfs.h"
#include "dmod.h"

int main(void) {
    // Initialize DMVFS with 16 mount points and 32 max open files
    if (!dmvfs_init(16, 32)) {
        printf("Failed to initialize DMVFS\n");
        return -1;
    }

    // Mount a file system at /mnt
    // "fatfs" is the name of a DMOD module implementing DMFSI
    if (!dmvfs_mount_fs("fatfs", "/mnt", "device=/dev/sda1")) {
        printf("Failed to mount file system\n");
        dmvfs_deinit();
        return -1;
    }

    // Open a file for writing
    void* fp = NULL;
    if (dmvfs_fopen(&fp, "/mnt/test.txt", DMFSI_O_CREAT | DMFSI_O_WRONLY, 0, 0) == 0) {
        const char* data = "Hello, DMVFS!";
        size_t written = 0;
        dmvfs_fwrite(fp, data, strlen(data), &written);
        dmvfs_fclose(fp);
    }

    // Read the file back
    if (dmvfs_fopen(&fp, "/mnt/test.txt", DMFSI_O_RDONLY, 0, 0) == 0) {
        char buffer[256] = {0};
        size_t read_bytes = 0;
        dmvfs_fread(fp, buffer, sizeof(buffer), &read_bytes);
        printf("Read: %s\n", buffer);
        dmvfs_fclose(fp);
    }

    // Unmount and cleanup
    dmvfs_unmount_fs("/mnt");
    dmvfs_deinit();
    
    return 0;
}
```

### API Overview

#### Initialization
- `dmvfs_init(max_mount_points, max_open_files)` - Initialize the VFS
- `dmvfs_deinit()` - Clean up and deinitialize

#### Mount Management
- `dmvfs_mount_fs(fs_name, mount_point, config)` - Mount a file system
- `dmvfs_unmount_fs(mount_point)` - Unmount a file system

#### File Operations
- `dmvfs_fopen(fp, path, mode, attr, pid)` - Open a file
- `dmvfs_fclose(fp)` - Close a file
- `dmvfs_fread(fp, buffer, size, read_bytes)` - Read from a file
- `dmvfs_fwrite(fp, buffer, size, written_bytes)` - Write to a file
- `dmvfs_lseek(fp, offset, whence)` - Seek to a position
- `dmvfs_ftell(fp)` - Get current position
- `dmvfs_feof(fp)` - Check for end-of-file
- `dmvfs_fflush(fp)` - Flush file buffers

#### Directory Operations
- `dmvfs_mkdir(path, mode)` - Create a directory
- `dmvfs_rmdir(path)` - Remove a directory
- `dmvfs_opendir(dp, path)` - Open a directory
- `dmvfs_readdir(dp, entry)` - Read directory entry
- `dmvfs_closedir(dp)` - Close a directory
- `dmvfs_chdir(path)` - Change current directory
- `dmvfs_getcwd(buffer, size)` - Get current working directory

#### File Management
- `dmvfs_stat(path, stat)` - Get file/directory information
- `dmvfs_rename(oldpath, newpath)` - Rename a file
- `dmvfs_unlink(path)` - Delete a file
- `dmvfs_chmod(path, mode)` - Change file permissions
- `dmvfs_utime(path, atime, mtime)` - Update file timestamps

For complete API documentation, see `inc/dmvfs.h`.

## Testing

DMVFS includes a comprehensive test suite that validates file system implementations.

### Building and Running Tests

```bash
# Build the project (if not already done)
cd build
cmake --build .

# Build the test file system module
cd ../tests/testfs/build
cmake .. -DDMOD_MODE=DMOD_MODULE -DDMOD_DIR=../../../build/_deps/dmod-src
cmake --build .

# Run the test suite
cd ../../../build
./tests/fs_tester ../tests/testfs/build/dmf/testfs.dmf
```

### Test Modes

The test suite supports different modes:

#### Read-Write Mode (Default)
Tests all file system operations including creation, modification, and deletion:
```bash
./tests/fs_tester path/to/filesystem.dmf
```

#### Read-Only Mode
Tests only read operations on existing files and directories:
```bash
./tests/fs_tester --read-only-fs \
  --test-file /mnt/existing_file.txt \
  --test-dir /mnt/existing_directory \
  path/to/filesystem.dmf
```

## Integration into Your Project

### Using CMake

Add DMVFS as a subdirectory or use FetchContent:

```cmake
include(FetchContent)

FetchContent_Declare(
    dmvfs
    GIT_REPOSITORY https://github.com/choco-technologies/dmvfs.git
    GIT_TAG        master
)

FetchContent_MakeAvailable(dmvfs)

# Link to your target
target_link_libraries(your_target PRIVATE dmvfs)
```

### Manual Integration

1. Include the `inc/` directory in your include path
2. Link against `libdmvfs.a`
3. Ensure DMOD and DMFSI are also linked

## Project Structure

```
dmvfs/
├── inc/                    # Public header files
│   └── dmvfs.h            # Main DMVFS API
├── src/                    # Source files
│   └── dmvfs.c            # DMVFS implementation
├── tests/                  # Test suite
│   ├── main.c             # Test runner
│   └── testfs/            # Example test file system
├── CMakeLists.txt         # Build configuration
├── LICENSE                # MIT License
└── README.md              # This file
```

## Contributing

Contributions are welcome! Please feel free to submit issues, fork the repository, and create pull requests.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Related Projects

- **[DMOD](https://github.com/choco-technologies/dmod)** - Dynamic module system for embedded systems
- **[DMFSI](https://github.com/choco-technologies/dmfsi)** - File system interface specification

## Authors

- Patryk Kubiak
- Choco-Technologies Team

## Support

For questions, issues, or feature requests, please open an issue on GitHub.
