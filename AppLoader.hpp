/*
 * AppLoader.hpp - CRTOS Dynamic Application Loader
 * Author: Arkadiusz Szlanta
 * Date: 05 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Dynamic application loader for user-space applications.
 * Loads relocatable ELF objects (.app files) and runs them as tasks.
 * Applications are compiled as Position Independent Code (PIC) and
 * use the kernel API for system services.
 */

#ifndef APP_LOADER_HPP
#define APP_LOADER_HPP

#include <cstdint>
#include <cstddef>
#include "Task.hpp"

namespace CRTOS
{

// Application load result
enum class AppResult : int32_t
{
    SUCCESS                 = 0,
    ERROR_INVALID_ELF       = -1,
    ERROR_NOT_RELOCATABLE   = -2,
    ERROR_WRONG_ARCH        = -3,
    ERROR_NO_MEMORY         = -4,
    ERROR_SYMBOL_NOT_FOUND  = -5,
    ERROR_RELOCATION_FAILED = -6,
    ERROR_NO_ENTRY          = -7,
    ERROR_INIT_FAILED       = -8,
    ERROR_FILE_READ         = -9,
    ERROR_ALREADY_LOADED    = -10,
    ERROR_TASK_CREATE       = -11
};

// Maximum number of loaded applications
constexpr size_t MAX_APPS = 8;

// Default stack size for applications (in bytes)
constexpr size_t DEFAULT_APP_STACK_SIZE = 4096;

/**
 * @brief Application entry point function type
 *
 * Applications must export a function matching this signature.
 * The function is called as the task's main entry point.
 *
 * @param args    Arguments passed from RunApp()
 * @return        Application exit code (0 = success)
 */
typedef int (*AppEntryFunc)(void* args);

/**
 * @brief Loaded application information
 */
struct LoadedApp
{
    char                name[32];       // Application name
    void*               baseAddress;    // Base address of loaded code
    size_t              size;           // Total allocated size
    void*               got;            // Global Offset Table (if separate)
    size_t              gotSize;        // GOT size in bytes
    uint32_t            gotBase;        // GOT base address for r9
    uint32_t*           sectionAddrs;   // Section addresses for relocations
    uint16_t            sectionCount;   // Number of sections
    AppEntryFunc        entryPoint;     // Application entry function
    Task::TaskHandle    taskHandle;     // Task handle for running app
    bool                isLoaded;       // Whether app is loaded
    bool                isRunning;      // Whether app task is active
};

/**
 * @brief Application API structure
 *
 * Provides system services to user applications.
 * Similar to KernelAPI but tailored for user-space apps.
 */
struct AppAPI
{
    // Version
    uint32_t version;
    
    // Task control
    void (*yield)(void);
    void (*delay)(uint32_t ms);
    uint32_t (*get_tick_count)(void);
    void (*exit)(int exitCode);
    
    // Console I/O
    int (*printf)(const char* fmt, ...);
    int (*getchar)(void);
    int (*putchar)(int c);
    
    // Memory (limited for apps)
    void* (*malloc)(size_t size);
    void (*free)(void* ptr);
    
    // Driver access (optional - apps request drivers by name)
    void* (*get_driver)(const char* name);
};

/**
 * @brief Dynamic Application Loader
 *
 * Loads relocatable ELF applications and runs them as tasks in USER mode.
 * Applications are compiled as PIC and use the AppAPI for system services.
 */
class AppLoader
{
public:
    /**
     * @brief Get singleton instance
     */
    static AppLoader& getInstance();

    /**
     * @brief Initialize the application loader
     *
     * Sets up the AppAPI and prepares for loading applications.
     *
     * @return AppResult::SUCCESS on success
     */
    AppResult init();

    /**
     * @brief Load an application from memory buffer
     *
     * Parses ELF, allocates memory, resolves relocations.
     * Does NOT start the application - call runApp() for that.
     *
     * @param elfData   Pointer to ELF data in memory
     * @param elfSize   Size of ELF data
     * @param appName   Name for the application
     * @return AppResult::SUCCESS on success
     */
    AppResult loadApp(const uint8_t* elfData, size_t elfSize, const char* appName);

    /**
     * @brief Load an application from file path
     *
     * @param filePath  Path to application file (e.g., "/apps/hello.app")
     * @return AppResult::SUCCESS on success
     */
    AppResult loadAppFromFile(const char* filePath);

    /**
     * @brief Run a loaded application as a task
     *
     * Creates a task in USER mode and starts the application.
     *
     * @param appName   Application name
     * @param args      Arguments passed to application
     * @param priority  Task priority (default: 3)
     * @param stackSize Stack size in bytes (default: 4096)
     * @return AppResult::SUCCESS on success
     */
    AppResult runApp(const char* appName, void* args = nullptr, 
                     uint32_t priority = 3, size_t stackSize = DEFAULT_APP_STACK_SIZE);

    /**
     * @brief Load and run an application in one call
     *
     * Convenience function that loads and immediately runs an app.
     *
     * @param filePath  Path to application file
     * @param args      Arguments passed to application
     * @param priority  Task priority
     * @param stackSize Stack size in bytes
     * @return AppResult::SUCCESS on success
     */
    AppResult loadAndRunApp(const char* filePath, void* args = nullptr,
                            uint32_t priority = 3, size_t stackSize = DEFAULT_APP_STACK_SIZE);

    /**
     * @brief Stop a running application
     *
     * Terminates the application's task.
     *
     * @param appName   Application name
     * @return AppResult::SUCCESS on success
     */
    AppResult stopApp(const char* appName);

    /**
     * @brief Unload an application
     *
     * Stops if running and frees all resources.
     *
     * @param appName   Application name
     * @return AppResult::SUCCESS on success
     */
    AppResult unloadApp(const char* appName);

    /**
     * @brief Find a loaded application by name
     *
     * @param appName   Application name
     * @return Pointer to LoadedApp, or nullptr if not found
     */
    LoadedApp* findApp(const char* appName);

    /**
     * @brief Get number of loaded applications
     */
    size_t getAppCount() const { return _appCount; }

    /**
     * @brief Get the application API
     *
     * Returns pointer to the AppAPI structure for use by applications.
     */
    const AppAPI* getAppAPI() const { return &_appAPI; }

    /**
     * @brief List all loaded applications (debug)
     */
    void listApps();

private:
    AppLoader();
    ~AppLoader();

    // Prevent copying
    AppLoader(const AppLoader&) = delete;
    AppLoader& operator=(const AppLoader&) = delete;

    // ELF parsing and loading (reuses ModuleLoader logic)
    bool validateElf(const uint8_t* elfData, size_t elfSize);
    AppResult loadElfSections(LoadedApp* app, const uint8_t* elfData, size_t elfSize);
    AppResult processRelocations(LoadedApp* app, const uint8_t* elfData, size_t elfSize);
    void* resolveSymbol(const char* name);

    // Task wrapper for running app entry
    static void appTaskWrapper(void* param);

    // Application API
    AppAPI _appAPI;

    // Loaded applications
    LoadedApp _apps[MAX_APPS];
    size_t _appCount;

    bool _initialized;
};

/**
 * @brief Get the AppAPI pointer
 *
 * Applications call this to get access to system services.
 * The function is exported as a kernel symbol.
 */
extern "C" const AppAPI* crtos_get_app_api(void);

} // namespace CRTOS

#endif // APP_LOADER_HPP
