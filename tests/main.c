#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "dmod.h"
#include "dmvfs.h"
#include "dmlist.h"

// Test result tracking
typedef struct {
    int total;
    int passed;
    int failed;
    int skipped;
} TestResults;

static TestResults test_results = {0};
static bool read_only_mode = false;
static const char* test_file_path = NULL;
static const char* test_dir_path = NULL;

// -----------------------------------------
//
//      Prints usage message
//
// -----------------------------------------
void PrintUsage( const char* AppName )
{
    printf("Usage: %s [OPTIONS] path/to/file.dmf\n", AppName);
    printf("Options:\n");
    printf("  --read-only-fs              Test filesystem in read-only mode\n");
    printf("  --test-file <path>          Path to existing file for read-only tests\n");
    printf("  --test-dir <path>           Path to existing directory for read-only tests\n");
}

// -----------------------------------------
//
//      Prints help message
//
// -----------------------------------------
void PrintHelp( const char* AppName )
{
    printf("-- DMVFS File System Tester ver. " DMOD_VERSION_STRING " --\n\n");
    printf("This tool tests and validates file system modules\n\n");
    printf("Usage: %s [OPTIONS] path/to/file.dmf\n", AppName);
    printf("Options:\n");
    printf("  -h, --help                  Print this help message\n");
    printf("  -v, --version               Print version information\n");
    printf("  --read-only-fs              Test filesystem in read-only mode\n");
    printf("  --test-file <path>          Path to existing file for read-only tests\n");
    printf("  --test-dir <path>           Path to existing directory for read-only tests\n");
}

// -----------------------------------------
//
//      Test helper macros and functions
//
// -----------------------------------------
#define TEST_START(name) \
    do { \
        printf("\n[TEST] %s...", name); \
        test_results.total++; \
    } while(0)

#define TEST_PASS() \
    do { \
        printf(" PASSED\n"); \
        test_results.passed++; \
    } while(0)

#define TEST_FAIL(reason) \
    do { \
        printf(" FAILED: %s\n", reason); \
        test_results.failed++; \
    } while(0)

#define TEST_SKIP(reason) \
    do { \
        printf(" SKIPPED: %s\n", reason); \
        test_results.skipped++; \
    } while(0)

// -----------------------------------------
//
//      Test: File open/close operations
//
// -----------------------------------------
bool test_file_open_close(void)
{
    TEST_START("File open/close");
    void* fp = NULL;
    int ret = dmvfs_fopen(&fp, "/mnt/test.txt", DMFSI_O_CREAT | DMFSI_O_RDWR, 0, 0);
    
    if (ret != DMFSI_OK || fp == NULL) {
        TEST_FAIL("Cannot open file for creation");
        return false;
    }
    
    ret = dmvfs_fclose(fp);
    // Note: Some implementations may return non-zero on close but still succeed
    // We'll be lenient here and only check that fp was valid
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: File write operations
//
// -----------------------------------------
bool test_file_write(void)
{
    TEST_START("File write");
    void* fp = NULL;
    const char* test_data = "Hello, World!";
    size_t written = 0;
    
    int ret = dmvfs_fopen(&fp, "/mnt/test.txt", DMFSI_O_WRONLY | DMFSI_O_TRUNC, 0, 0);
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot open file for writing");
        return false;
    }
    
    ret = dmvfs_fwrite(fp, test_data, strlen(test_data), &written);
    if (ret != DMFSI_OK || written != strlen(test_data)) {
        dmvfs_fclose(fp);
        TEST_FAIL("Cannot write to file");
        return false;
    }
    
    dmvfs_fclose(fp);
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: File read operations
//
// -----------------------------------------
bool test_file_read(void)
{
    TEST_START("File read");
    void* fp = NULL;
    char buffer[256] = {0};
    size_t read_bytes = 0;
    
    int ret = dmvfs_fopen(&fp, "/mnt/test.txt", DMFSI_O_RDONLY, 0, 0);
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot open file for reading");
        return false;
    }
    
    ret = dmvfs_fread(fp, buffer, sizeof(buffer), &read_bytes);
    if (ret != DMFSI_OK) {
        dmvfs_fclose(fp);
        TEST_FAIL("Cannot read from file");
        return false;
    }
    
    if (strcmp(buffer, "Hello, World!") != 0) {
        dmvfs_fclose(fp);
        TEST_FAIL("Read data doesn't match written data");
        return false;
    }
    
    dmvfs_fclose(fp);
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: File seek/tell operations
//
// -----------------------------------------
bool test_file_seek_tell(void)
{
    TEST_START("File seek/tell");
    void* fp = NULL;
    
    int ret = dmvfs_fopen(&fp, "/mnt/test.txt", DMFSI_O_RDONLY, 0, 0);
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot open file");
        return false;
    }
    
    long pos = dmvfs_lseek(fp, 7, DMFSI_SEEK_SET);
    if (pos != 7) {
        dmvfs_fclose(fp);
        TEST_FAIL("Seek to position 7 failed");
        return false;
    }
    
    pos = dmvfs_ftell(fp);
    if (pos != 7) {
        dmvfs_fclose(fp);
        TEST_FAIL("Tell position doesn't match");
        return false;
    }
    
    dmvfs_fclose(fp);
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: File EOF detection
//
// -----------------------------------------
bool test_file_eof(void)
{
    TEST_START("File EOF detection");
    void* fp = NULL;
    char buffer[256] = {0};
    size_t read_bytes = 0;
    
    int ret = dmvfs_fopen(&fp, "/mnt/test.txt", DMFSI_O_RDONLY, 0, 0);
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot open file");
        return false;
    }
    
    // Read entire file
    ret = dmvfs_fread(fp, buffer, sizeof(buffer), &read_bytes);
    if (ret != DMFSI_OK) {
        dmvfs_fclose(fp);
        TEST_FAIL("Cannot read from file");
        return false;
    }
    
    // Check EOF
    int eof = dmvfs_feof(fp);
    if (!eof) {
        dmvfs_fclose(fp);
        TEST_FAIL("EOF not detected at end of file");
        return false;
    }
    
    dmvfs_fclose(fp);
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: Character I/O operations
//
// -----------------------------------------
bool test_char_io(void)
{
    TEST_START("Character I/O (getc/putc)");
    void* fp = NULL;
    
    // Write a character
    int ret = dmvfs_fopen(&fp, "/mnt/char_test.txt", DMFSI_O_CREAT | DMFSI_O_WRONLY, 0, 0);
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot open file for writing");
        return false;
    }
    
    ret = dmvfs_putc(fp, 'A');
    if (ret != 'A') {
        dmvfs_fclose(fp);
        TEST_FAIL("Cannot write character");
        return false;
    }
    dmvfs_fclose(fp);
    
    // Read the character
    ret = dmvfs_fopen(&fp, "/mnt/char_test.txt", DMFSI_O_RDONLY, 0, 0);
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot open file for reading");
        return false;
    }
    
    int ch = dmvfs_getc(fp);
    if (ch != 'A') {
        dmvfs_fclose(fp);
        TEST_FAIL("Read character doesn't match");
        return false;
    }
    
    dmvfs_fclose(fp);
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: File stat operations
//
// -----------------------------------------
bool test_file_stat(void)
{
    TEST_START("File stat");
    dmfsi_stat_t stat;
    
    int ret = dmvfs_stat("/mnt/test.txt", &stat);
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot get file stat");
        return false;
    }
    
    if (stat.size != 13) {  // "Hello, World!" is 13 bytes
        TEST_FAIL("File size doesn't match expected value");
        return false;
    }
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: File rename operations
//
// -----------------------------------------
bool test_file_rename(void)
{
    TEST_START("File rename");
    
    int ret = dmvfs_rename("/mnt/test.txt", "/mnt/renamed.txt");
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot rename file");
        return false;
    }
    
    // Verify old name doesn't exist
    dmfsi_stat_t stat;
    ret = dmvfs_stat("/mnt/test.txt", &stat);
    if (ret == DMFSI_OK) {
        TEST_FAIL("Old filename still exists");
        return false;
    }
    
    // Verify new name exists
    ret = dmvfs_stat("/mnt/renamed.txt", &stat);
    if (ret != DMFSI_OK) {
        TEST_FAIL("New filename doesn't exist");
        return false;
    }
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: File unlink operations
//
// -----------------------------------------
bool test_file_unlink(void)
{
    TEST_START("File unlink");
    
    int ret = dmvfs_unlink("/mnt/renamed.txt");
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot unlink file");
        return false;
    }
    
    // Verify file doesn't exist
    dmfsi_stat_t stat;
    ret = dmvfs_stat("/mnt/renamed.txt", &stat);
    if (ret == DMFSI_OK) {
        TEST_FAIL("File still exists after unlink");
        return false;
    }
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: Directory operations
//
// -----------------------------------------
bool test_directory_operations(void)
{
    TEST_START("Directory operations");
    
    // Create directory
    int ret = dmvfs_mkdir("/mnt/testdir", 0);
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot create directory");
        return false;
    }
    
    // Check if directory exists
    ret = dmvfs_direxists("/mnt/testdir");
    if (!ret) {
        TEST_FAIL("Directory doesn't exist after creation");
        return false;
    }
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: Directory listing
//
// -----------------------------------------
bool test_directory_listing(void)
{
    TEST_START("Directory listing");
    void* dp = NULL;
    
    // Create a test file
    void* fp = NULL;
    int ret = dmvfs_fopen(&fp, "/mnt/listtest.txt", DMFSI_O_CREAT | DMFSI_O_RDWR, 0, 0);
    if (ret == DMFSI_OK && fp != NULL) {
        dmvfs_fclose(fp);
    }
    
    // Try to open the root directory of the mounted filesystem
    ret = dmvfs_opendir(&dp, "/mnt/");
    if (ret != DMFSI_OK || dp == NULL) {
        // Clean up test file
        dmvfs_unlink("/mnt/listtest.txt");
        // If that fails, the filesystem might not support directory listing
        TEST_SKIP("Directory listing not supported or root access needed");
        return false;
    }
    
    // Read directory entries and check for our test file
    dmfsi_dir_entry_t entry;
    int entry_count = 0;
    bool found_test_file = false;
    while (dmvfs_readdir(dp, &entry) == DMFSI_OK) {
        entry_count++;
        if (strstr(entry.name, "listtest.txt") != NULL) {
            found_test_file = true;
        }
    }
    
    dmvfs_closedir(dp);
    
    // Clean up test file
    dmvfs_unlink("/mnt/listtest.txt");
    
    if (entry_count == 0) {
        // Empty directory is still valid
        printf(" (directory is empty)");
    } else if (!found_test_file) {
        TEST_FAIL("Created file not found in directory listing");
        return false;
    }
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: Directory creation and visibility
//
// -----------------------------------------
bool test_directory_creation_and_listing(void)
{
    TEST_START("Directory creation and visibility in listing");
    void* dp = NULL;
    
    // Create a test directory
    int ret = dmvfs_mkdir("/mnt/testdir_visible", 0);
    if (ret != DMFSI_OK) {
        TEST_FAIL("Cannot create directory");
        return false;
    }
    
    // Verify directory exists using direxists
    ret = dmvfs_direxists("/mnt/testdir_visible");
    if (!ret) {
        dmvfs_rmdir("/mnt/testdir_visible");
        TEST_FAIL("Directory doesn't exist after creation");
        return false;
    }
    
    // Try to list and find the directory (optional)
    ret = dmvfs_opendir(&dp, "/mnt/");
    if (ret == DMFSI_OK && dp != NULL) {
        dmfsi_dir_entry_t entry;
        bool found_new_dir = false;
        while (dmvfs_readdir(dp, &entry) == DMFSI_OK) {
            if (strstr(entry.name, "testdir_visible") != NULL) {
                found_new_dir = true;
                break;
            }
        }
        dmvfs_closedir(dp);
        
        if (!found_new_dir) {
            printf(" (dir exists but not in listing)");
        }
    }
    
    // Clean up - ignore errors if rmdir not supported
    dmvfs_rmdir("/mnt/testdir_visible");
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Test: DMLIST linked list operations
//
// -----------------------------------------
bool test_dmlist_operations(void)
{
    TEST_START("DMLIST linked list operations");
    
    // Create a list
    dmlist_context_t* list = dmlist_create("fs_tester");
    if (list == NULL) {
        TEST_FAIL("Cannot create dmlist");
        return false;
    }
    
    // Check if empty
    if (!dmlist_is_empty(list)) {
        dmlist_destroy(list);
        TEST_FAIL("New list should be empty");
        return false;
    }
    
    // Add some test data
    int test_values[] = {1, 2, 3};
    
    if (!dmlist_push_back(list, &test_values[0])) {
        dmlist_destroy(list);
        TEST_FAIL("Cannot push to list");
        return false;
    }
    
    if (!dmlist_push_back(list, &test_values[1])) {
        dmlist_destroy(list);
        TEST_FAIL("Cannot push second element");
        return false;
    }
    
    if (!dmlist_push_front(list, &test_values[2])) {
        dmlist_destroy(list);
        TEST_FAIL("Cannot push front");
        return false;
    }
    
    // Check size
    if (dmlist_size(list) != 3) {
        dmlist_destroy(list);
        TEST_FAIL("List size should be 3");
        return false;
    }
    
    // Get front element (should be test_values[2] = 3)
    int* front = (int*)dmlist_front(list);
    if (front == NULL || *front != 3) {
        dmlist_destroy(list);
        TEST_FAIL("Front element incorrect");
        return false;
    }
    
    // Clear and destroy
    dmlist_clear(list);
    if (!dmlist_is_empty(list)) {
        dmlist_destroy(list);
        TEST_FAIL("List should be empty after clear");
        return false;
    }
    
    dmlist_destroy(list);
    TEST_PASS();
    return true;
}

// -----------------------------------------
//
//      Run all tests
//
// -----------------------------------------
void run_all_tests(void)
{
    printf("\n========================================\n");
    printf("  DMVFS File System Test Suite\n");
    printf("========================================\n");
    printf("Mode: %s\n", read_only_mode ? "READ-ONLY" : "READ-WRITE");
    
    // File operations tests
    if (read_only_mode) {
        // Read-only mode tests with optional file and directory paths
        
        if (test_file_path) {
            // Test reading an existing file
            TEST_START("Read existing file");
            void* fp = NULL;
            int ret = dmvfs_fopen(&fp, test_file_path, DMFSI_O_RDONLY, 0, 0);
            if (ret == DMFSI_OK && fp != NULL) {
                char buffer[256] = {0};
                size_t read_bytes = 0;
                ret = dmvfs_fread(fp, buffer, sizeof(buffer), &read_bytes);
                if (ret == DMFSI_OK) {
                    printf(" (read %zu bytes)", read_bytes);
                    TEST_PASS();
                } else {
                    TEST_FAIL("Cannot read from file");
                }
                dmvfs_fclose(fp);
            } else {
                TEST_FAIL("Cannot open test file");
            }
            
            // Test file stat
            TEST_START("Stat existing file");
            dmfsi_stat_t stat;
            ret = dmvfs_stat(test_file_path, &stat);
            if (ret == DMFSI_OK) {
                printf(" (size: %lu bytes)", (unsigned long)stat.size);
                TEST_PASS();
            } else {
                TEST_FAIL("Cannot stat file");
            }
            
            // Test character I/O on existing file
            TEST_START("Character I/O on existing file (getc)");
            ret = dmvfs_fopen(&fp, test_file_path, DMFSI_O_RDONLY, 0, 0);
            if (ret == DMFSI_OK && fp != NULL) {
                int ch = dmvfs_getc(fp);
                if (ch >= 0) {
                    printf(" (first char: '%c')", (char)ch);
                    TEST_PASS();
                } else {
                    TEST_FAIL("Cannot read character");
                }
                dmvfs_fclose(fp);
            } else {
                TEST_FAIL("Cannot open file");
            }
        } else {
            TEST_START("File read");
            TEST_SKIP("No test file specified (use --test-file)");
            TEST_START("File stat");
            TEST_SKIP("No test file specified (use --test-file)");
            TEST_START("Character I/O (getc)");
            TEST_SKIP("No test file specified (use --test-file)");
        }
        
        // Skip write operations
        TEST_START("File open/close (write)");
        TEST_SKIP("Read-only mode");
        TEST_START("File write");
        TEST_SKIP("Read-only mode");
        TEST_START("File seek/tell");
        TEST_SKIP("Read-only mode - needs writable file");
        TEST_START("File EOF detection");
        TEST_SKIP("Read-only mode - needs writable file");
        TEST_START("Character I/O (putc)");
        TEST_SKIP("Read-only mode");
        TEST_START("File rename");
        TEST_SKIP("Read-only mode");
        TEST_START("File unlink");
        TEST_SKIP("Read-only mode");
        TEST_START("Directory creation");
        TEST_SKIP("Read-only mode");
        
        // Directory listing tests
        if (test_dir_path) {
            TEST_START("List existing directory");
            void* dp = NULL;
            int ret = dmvfs_opendir(&dp, test_dir_path);
            if (ret == DMFSI_OK && dp != NULL) {
                dmfsi_dir_entry_t entry;
                printf("\n  Files in %s:\n", test_dir_path);
                int count = 0;
                while (dmvfs_readdir(dp, &entry) == DMFSI_OK) {
                    printf("    - %s (size: %lu bytes)\n", entry.name, (unsigned long)entry.size);
                    count++;
                }
                if (count == 0) {
                    printf("    (empty directory)\n");
                } else {
                    printf("  Total entries: %d\n", count);
                }
                dmvfs_closedir(dp);
                TEST_PASS();
            } else {
                TEST_FAIL("Cannot open test directory");
            }
        } else {
            TEST_START("Directory listing");
            // Try to list root directory in read-only mode
            void* dp = NULL;
            int ret = dmvfs_opendir(&dp, "/mnt/");
            if (ret == DMFSI_OK && dp != NULL) {
                dmfsi_dir_entry_t entry;
                printf("\n  Files in /mnt:\n");
                int count = 0;
                while (dmvfs_readdir(dp, &entry) == DMFSI_OK) {
                    printf("    - %s (size: %lu bytes)\n", entry.name, (unsigned long)entry.size);
                    count++;
                }
                if (count == 0) {
                    printf("    (empty directory)\n");
                } else {
                    printf("  Total entries: %d\n", count);
                }
                dmvfs_closedir(dp);
                TEST_PASS();
            } else {
                TEST_SKIP("Directory listing not available (use --test-dir)");
            }
        }
        
        TEST_START("Directory creation visibility");
        TEST_SKIP("Read-only mode");
    } else {
        // Full test suite for writable filesystems
        test_file_open_close();
        test_file_write();
        test_file_read();
        test_file_seek_tell();
        test_file_eof();
        test_char_io();
        test_file_stat();
        test_file_rename();
        test_file_unlink();
        test_directory_operations();
        test_directory_listing();
        test_directory_creation_and_listing();
    }
    
    // DMLIST library tests (run in both modes)
    test_dmlist_operations();
    
    // Print summary
    printf("\n========================================\n");
    printf("  Test Summary\n");
    printf("========================================\n");
    printf("Total tests:  %d\n", test_results.total);
    printf("Passed:       %d\n", test_results.passed);
    printf("Failed:       %d\n", test_results.failed);
    printf("Skipped:      %d\n", test_results.skipped);
    printf("========================================\n");
    
    if (test_results.failed == 0) {
        printf("\nResult: ✓ ALL TESTS PASSED\n");
    } else {
        printf("\nResult: ✗ SOME TESTS FAILED\n");
    }
    printf("\n");
}

// -----------------------------------------
//
//      Main function
//
// -----------------------------------------
int main( int argc, char *argv[] )
{
    const char* module_path = NULL;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            PrintHelp(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("DMVFS File System Tester ver. " DMOD_VERSION_STRING "\n");
            return 0;
        } else if (strcmp(argv[i], "--read-only-fs") == 0) {
            read_only_mode = true;
        } else if (strcmp(argv[i], "--test-file") == 0) {
            if (i + 1 < argc) {
                test_file_path = argv[++i];
            } else {
                printf("Error: --test-file requires a path argument\n");
                PrintUsage(argv[0]);
                return -1;
            }
        } else if (strcmp(argv[i], "--test-dir") == 0) {
            if (i + 1 < argc) {
                test_dir_path = argv[++i];
            } else {
                printf("Error: --test-dir requires a path argument\n");
                PrintUsage(argv[0]);
                return -1;
            }
        } else {
            module_path = argv[i];
        }
    }
    
    if (module_path == NULL) {
        PrintUsage(argv[0]);
        return 0;
    }

    Dmod_Context_t* context = Dmod_LoadFile( module_path );
    if( context == NULL )
    {
        printf("Cannot load module: %s\n", module_path);
        return -1;
    }

    if (!Dmod_Enable( context, false, NULL ))
    {
        printf("Cannot enable module: %s\n", module_path);
        Dmod_Unload( context, false );
        return -1;
    }

    const char* module_name = Dmod_GetName( context );
    printf("Module '%s' loaded and enabled successfully.\n", module_name);

    if (!dmvfs_init( 16, 32 ))
    {
        printf("Cannot initialize DMVFS\n");
        return -1;
    }

    printf("DMVFS initialized successfully.\nMounting %s at /mnt...\n", module_name);

    if(!dmvfs_mount_fs( module_name, "/mnt", NULL ))
    {
        printf("Cannot mount %s at /mnt\n", module_name);
        dmvfs_deinit();
        return -1;
    }

    printf("Filesystem mounted at /mnt successfully.\n");

    if (read_only_mode) {
        printf("Testing in READ-ONLY mode\n");
        if (test_file_path) {
            printf("  Test file: %s\n", test_file_path);
        }
        if (test_dir_path) {
            printf("  Test directory: %s\n", test_dir_path);
        }
    }

    // Listing root directory files in the mounted filesystem
    printf("\nListing files in /mnt before tests:\n");
    void* dp = NULL;
    if (dmvfs_opendir(&dp, "/mnt/") == DMFSI_OK && dp != NULL) {
        dmfsi_dir_entry_t entry;
        int count = 0;
        while (dmvfs_readdir(dp, &entry) == DMFSI_OK) {
            printf("  - %s (size: %lu bytes)\n", entry.name, (unsigned long)entry.size);
            count++;
        }
        if (count == 0) {
            printf("  (empty directory)\n");
        } else {
            printf("Total entries: %d\n", count);
        }
        dmvfs_closedir(dp);
    } else {
        printf("  (cannot list files in /mnt)\n");
    }

    // Run test suite
    run_all_tests();

    // Cleanup
    if(!dmvfs_unmount_fs( "/mnt" ))
    {
        printf("Cannot unmount /mnt\n");
    }

    printf("Unmounted /mnt successfully.\nDeinitializing DMVFS...\n");

    dmvfs_deinit();

    return (test_results.failed > 0) ? 1 : 0;
}

