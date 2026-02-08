/**
 * Unit test for mount point visibility helper functions
 * Tests the is_direct_child_mount and get_basename functions
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

// Copy the helper functions from dmvfs.c for testing
static bool is_direct_child_mount(const char* dir_path, const char* mount_path)
{
    if (!dir_path || !mount_path) {
        return false;
    }
    
    size_t dir_len = strlen(dir_path);
    size_t mount_len = strlen(mount_path);
    
    // Mount path must be longer than directory path
    if (mount_len <= dir_len) {
        return false;
    }
    
    // Check if mount_path starts with dir_path
    if (strncmp(dir_path, mount_path, dir_len) != 0) {
        return false;
    }
    
    // For root directory "/", check that mount path doesn't have more than one additional slash
    if (dir_len == 1 && dir_path[0] == '/') {
        // Find first slash after root
        const char* next_slash = strchr(mount_path + 1, '/');
        // It's a direct child if there's no slash after the first character
        return (next_slash == NULL);
    }
    
    // For non-root directories, ensure the mount point is exactly one level deeper
    // mount_path should be: dir_path + "/" + name (no additional slashes)
    const char* remainder = mount_path + dir_len;
    if (remainder[0] != '/') {
        return false;
    }
    
    // Check that there are no more slashes after this point
    const char* next_slash = strchr(remainder + 1, '/');
    return (next_slash == NULL);
}

static void get_basename(const char* path, char* buffer, size_t buffer_size)
{
    if (!path || !buffer || buffer_size == 0) {
        return;
    }
    
    // Find the last slash
    const char* last_slash = strrchr(path, '/');
    if (!last_slash) {
        // No slash found, use the entire path
        strncpy(buffer, path, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }
    
    // If it's the root "/" itself
    if (last_slash == path && last_slash[1] == '\0') {
        buffer[0] = '\0';
        return;
    }
    
    // Copy everything after the last slash
    strncpy(buffer, last_slash + 1, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
}

// Test framework
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, test_name) \
    do { \
        if (condition) { \
            printf("✓ PASS: %s\n", test_name); \
            tests_passed++; \
        } else { \
            printf("✗ FAIL: %s\n", test_name); \
            tests_failed++; \
        } \
    } while(0)

void test_is_direct_child_mount() {
    printf("\n=== Testing is_direct_child_mount ===\n");
    
    // Test root directory "/"
    TEST_ASSERT(is_direct_child_mount("/", "/configs") == true, 
                "/ is parent of /configs");
    TEST_ASSERT(is_direct_child_mount("/", "/mnt") == true, 
                "/ is parent of /mnt");
    TEST_ASSERT(is_direct_child_mount("/", "/a") == true, 
                "/ is parent of /a");
    
    // Test that nested paths are NOT direct children
    TEST_ASSERT(is_direct_child_mount("/", "/configs/app") == false, 
                "/ is NOT direct parent of /configs/app");
    TEST_ASSERT(is_direct_child_mount("/", "/a/b/c") == false, 
                "/ is NOT direct parent of /a/b/c");
    
    // Test non-root directories
    TEST_ASSERT(is_direct_child_mount("/configs", "/configs/app") == true, 
                "/configs is parent of /configs/app");
    TEST_ASSERT(is_direct_child_mount("/mnt", "/mnt/sd") == true, 
                "/mnt is parent of /mnt/sd");
    
    // Test that deeply nested paths are NOT direct children
    TEST_ASSERT(is_direct_child_mount("/configs", "/configs/app/data") == false, 
                "/configs is NOT direct parent of /configs/app/data");
    
    // Test edge cases
    TEST_ASSERT(is_direct_child_mount("/", "/") == false, 
                "/ is NOT parent of itself");
    TEST_ASSERT(is_direct_child_mount("/configs", "/configs") == false, 
                "/configs is NOT parent of itself");
    TEST_ASSERT(is_direct_child_mount("/con", "/configs") == false, 
                "Partial match should not work");
    TEST_ASSERT(is_direct_child_mount("/configs", "/mnt") == false, 
                "Unrelated paths should not match");
}

void test_get_basename() {
    printf("\n=== Testing get_basename ===\n");
    char buffer[256];
    
    get_basename("/configs", buffer, sizeof(buffer));
    TEST_ASSERT(strcmp(buffer, "configs") == 0, 
                "Basename of /configs is 'configs'");
    
    get_basename("/mnt", buffer, sizeof(buffer));
    TEST_ASSERT(strcmp(buffer, "mnt") == 0, 
                "Basename of /mnt is 'mnt'");
    
    get_basename("/a/b/c", buffer, sizeof(buffer));
    TEST_ASSERT(strcmp(buffer, "c") == 0, 
                "Basename of /a/b/c is 'c'");
    
    get_basename("/", buffer, sizeof(buffer));
    TEST_ASSERT(buffer[0] == '\0', 
                "Basename of / is empty string");
    
    get_basename("/test.txt", buffer, sizeof(buffer));
    TEST_ASSERT(strcmp(buffer, "test.txt") == 0, 
                "Basename of /test.txt is 'test.txt'");
}

int main(void) {
    printf("========================================\n");
    printf("  Mount Point Helper Functions Test\n");
    printf("========================================\n");
    
    test_is_direct_child_mount();
    test_get_basename();
    
    printf("\n========================================\n");
    printf("  Test Summary\n");
    printf("========================================\n");
    printf("Total tests: %d\n", tests_passed + tests_failed);
    printf("Passed:      %d\n", tests_passed);
    printf("Failed:      %d\n", tests_failed);
    printf("========================================\n");
    
    if (tests_failed == 0) {
        printf("\n✓ ALL TESTS PASSED\n\n");
        return 0;
    } else {
        printf("\n✗ SOME TESTS FAILED\n\n");
        return 1;
    }
}
