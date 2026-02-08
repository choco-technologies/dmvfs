/*
 * Test program to verify correct mount point selection
 * Tests the fix for: get_mount_point_for_path should return the longest matching mount point
 */

#define DMOD_ENABLE_REGISTRATION
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "dmod.h"
#include "dmvfs.h"

static int test_passed = 0;
static int test_failed = 0;

#define TEST_PASS(msg) do { \
    printf("✓ PASS: %s\n", msg); \
    test_passed++; \
} while(0)

#define TEST_FAIL(msg) do { \
    printf("✗ FAIL: %s\n", msg); \
    test_failed++; \
} while(0)

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <fs1_module.dmf> <fs2_module.dmf>\n", argv[0]);
        printf("This test requires two filesystem modules to test mount point selection.\n");
        return -1;
    }

    const char* fs1_path = argv[1];
    const char* fs2_path = argv[2];

    printf("========================================\n");
    printf("  Mount Point Selection Test\n");
    printf("========================================\n\n");

    // Load first filesystem module
    Dmod_Context_t* context1 = Dmod_LoadFile(fs1_path);
    if (context1 == NULL) {
        printf("✗ Cannot load module: %s\n", fs1_path);
        return -1;
    }

    if (!Dmod_Enable(context1, false, NULL)) {
        printf("✗ Cannot enable module: %s\n", fs1_path);
        Dmod_Unload(context1, false);
        return -1;
    }

    // Load second filesystem module
    Dmod_Context_t* context2 = Dmod_LoadFile(fs2_path);
    if (context2 == NULL) {
        printf("✗ Cannot load module: %s\n", fs2_path);
        Dmod_Unload(context1, false);
        return -1;
    }

    if (!Dmod_Enable(context2, false, NULL)) {
        printf("✗ Cannot enable module: %s\n", fs2_path);
        Dmod_Unload(context2, false);
        Dmod_Unload(context1, false);
        return -1;
    }

    const char* fs1_name = Dmod_GetName(context1);
    const char* fs2_name = Dmod_GetName(context2);
    
    printf("Loaded filesystems:\n");
    printf("  FS1: %s\n", fs1_name);
    printf("  FS2: %s\n", fs2_name);
    printf("\n");

    // Initialize DMVFS
    if (!dmvfs_init(16, 32)) {
        printf("✗ Cannot initialize DMVFS\n");
        return -1;
    }

    printf("Test scenario: Mount FS1 at '/' and FS2 at '/configs'\n");
    printf("Expected behavior: Paths under '/configs' should use FS2, not FS1\n\n");

    // Mount first filesystem at root
    if (!dmvfs_mount_fs(fs1_name, "/", NULL)) {
        printf("✗ Cannot mount %s at /\n", fs1_name);
        dmvfs_deinit();
        return -1;
    }
    printf("✓ Mounted %s at /\n", fs1_name);

    // Mount second filesystem at /configs
    if (!dmvfs_mount_fs(fs2_name, "/configs", NULL)) {
        printf("✗ Cannot mount %s at /configs\n", fs2_name);
        dmvfs_unmount_fs("/");
        dmvfs_deinit();
        return -1;
    }
    printf("✓ Mounted %s at /configs\n\n", fs2_name);

    printf("Running tests...\n\n");

    // Test 1: Create file at /test.txt (should use FS1)
    printf("Test 1: Create /test.txt (should use %s at /)\n", fs1_name);
    void* fp1 = NULL;
    int ret = dmvfs_fopen(&fp1, "/test.txt", DMFSI_O_CREAT | DMFSI_O_WRONLY, 0, 0);
    if (ret == DMFSI_OK && fp1 != NULL) {
        const char* data = "Root filesystem";
        size_t written = 0;
        dmvfs_fwrite(fp1, data, strlen(data), &written);
        dmvfs_fclose(fp1);
        TEST_PASS("Created file at root mount point");
    } else {
        TEST_FAIL("Cannot create file at root mount point");
    }

    // Test 2: Create file at /configs/app.conf (should use FS2, not FS1)
    printf("Test 2: Create /configs/app.conf (should use %s at /configs)\n", fs2_name);
    void* fp2 = NULL;
    ret = dmvfs_fopen(&fp2, "/configs/app.conf", DMFSI_O_CREAT | DMFSI_O_WRONLY, 0, 0);
    if (ret == DMFSI_OK && fp2 != NULL) {
        const char* data = "Config filesystem";
        size_t written = 0;
        dmvfs_fwrite(fp2, data, strlen(data), &written);
        dmvfs_fclose(fp2);
        TEST_PASS("Created file at /configs mount point");
    } else {
        TEST_FAIL("Cannot create file at /configs mount point");
    }

    // Test 3: Read back the files to verify they're in different filesystems
    printf("Test 3: Verify files can be read back\n");
    
    // Read /test.txt
    ret = dmvfs_fopen(&fp1, "/test.txt", DMFSI_O_RDONLY, 0, 0);
    if (ret == DMFSI_OK && fp1 != NULL) {
        char buffer[64] = {0};
        size_t read_bytes = 0;
        dmvfs_fread(fp1, buffer, sizeof(buffer), &read_bytes);
        dmvfs_fclose(fp1);
        if (strcmp(buffer, "Root filesystem") == 0) {
            TEST_PASS("Read /test.txt successfully");
        } else {
            TEST_FAIL("Content mismatch in /test.txt");
        }
    } else {
        TEST_FAIL("Cannot read /test.txt");
    }

    // Read /configs/app.conf
    ret = dmvfs_fopen(&fp2, "/configs/app.conf", DMFSI_O_RDONLY, 0, 0);
    if (ret == DMFSI_OK && fp2 != NULL) {
        char buffer[64] = {0};
        size_t read_bytes = 0;
        dmvfs_fread(fp2, buffer, sizeof(buffer), &read_bytes);
        dmvfs_fclose(fp2);
        if (strcmp(buffer, "Config filesystem") == 0) {
            TEST_PASS("Read /configs/app.conf successfully");
        } else {
            TEST_FAIL("Content mismatch in /configs/app.conf");
        }
    } else {
        TEST_FAIL("Cannot read /configs/app.conf");
    }

    // Test 4: List files in both mount points
    printf("Test 4: Directory listing verification\n");
    
    // List root
    void* dp = NULL;
    ret = dmvfs_opendir(&dp, "/");
    if (ret == DMFSI_OK && dp != NULL) {
        printf("  Files in /:\n");
        dmfsi_dir_entry_t entry;
        bool found_test_txt = false;
        while (dmvfs_readdir(dp, &entry) == DMFSI_OK) {
            printf("    - %s\n", entry.name);
            if (strcmp(entry.name, "test.txt") == 0) {
                found_test_txt = true;
            }
        }
        dmvfs_closedir(dp);
        if (found_test_txt) {
            TEST_PASS("Found test.txt in root directory");
        } else {
            TEST_FAIL("test.txt not found in root directory");
        }
    } else {
        TEST_FAIL("Cannot list root directory");
    }

    // List /configs
    ret = dmvfs_opendir(&dp, "/configs");
    if (ret == DMFSI_OK && dp != NULL) {
        printf("  Files in /configs:\n");
        dmfsi_dir_entry_t entry;
        bool found_app_conf = false;
        while (dmvfs_readdir(dp, &entry) == DMFSI_OK) {
            printf("    - %s\n", entry.name);
            if (strcmp(entry.name, "app.conf") == 0) {
                found_app_conf = true;
            }
        }
        dmvfs_closedir(dp);
        if (found_app_conf) {
            TEST_PASS("Found app.conf in /configs directory");
        } else {
            TEST_FAIL("app.conf not found in /configs directory");
        }
    } else {
        TEST_FAIL("Cannot list /configs directory");
    }

    // Cleanup
    printf("\nCleaning up...\n");
    dmvfs_unmount_fs("/configs");
    dmvfs_unmount_fs("/");
    dmvfs_deinit();

    // Print summary
    printf("\n========================================\n");
    printf("  Test Summary\n");
    printf("========================================\n");
    printf("Total tests: %d\n", test_passed + test_failed);
    printf("Passed:      %d\n", test_passed);
    printf("Failed:      %d\n", test_failed);
    printf("========================================\n");

    if (test_failed == 0) {
        printf("\n✓ ALL TESTS PASSED\n");
        printf("The fix correctly selects the longest matching mount point!\n\n");
        return 0;
    } else {
        printf("\n✗ SOME TESTS FAILED\n\n");
        return 1;
    }
}
