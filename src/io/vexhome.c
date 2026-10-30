#include "io/vexhome.h"
#include "platform/platform.h"
// Production R2 I/O owner: Relational Engine; existing VexHome ABI preserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "io/file.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VexHome
 * ============================================================================
 * Owns the per-user engine home directory layout on disk (was AntiHome,
 * renamed on the vexspoke/darling split). Resolves the root per-platform —
 * $VEX_HOME test seam first, then macOS Application Support, Windows
 * LOCALAPPDATA, XDG data home, and finally $HOME/vex — and derives the
 * projects/logs/fonts/placeholder/cache subpaths into static FILE_PATH_MAX
 * buffers. VexHome_ensure creates the tree idempotently; VexHome_cacheEnsure
 * writes a never-clobbered dictionary.ini index. Legacy ~/vex and ~/anti are
 * never migrated or deleted. Lives at R2 as a leaf I/O behavior.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VexHome (io/vexhome.c)
 * ============================================================================
 * The VexHome class (was AntiHome, renamed on the vexspoke/darling split).
 * Manages the per-user engine home directory layout on disk.
 *
 * Root resolution precedence (implemented in resolve_base):
 *   1. $VEX_HOME (non-empty) — documented test seam, overrides everything
 *   2. macOS (VEX_PLATFORM_APPLE): $HOME/Library/Application Support/vexgraph
 *   3. Windows (VEX_PLATFORM_WINDOWS): %LOCALAPPDATA%\vexgraph (fallback %USERPROFILE%\vexgraph)
 *   4. Linux/other: $XDG_DATA_HOME/vexgraph (fallback $HOME/.local/share/vexgraph)
 *   5. Final fallback: $HOME/vex  (ROOT_NAME "vex" used only for this fallback)
 *
 * Cache paths derive from the resolved root: <root>/cache/<subsystem>/
 * with dictionary.ini created by VexHome_cacheEnsure (never clobbered).
 *
 * STRUCT FIELDS: none — procedural (operates on VexHome directory layout on disk)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   static char root_buf[FILE_PATH_MAX];        // VexHome_root buffer
 *   static char projects_buf[FILE_PATH_MAX];    // VexHome_projects buffer
 *   static char logs_buf[FILE_PATH_MAX];        // VexHome_logs / defaultLogPath buffer
 *   static char fonts_buf[FILE_PATH_MAX];       // VexHome_fonts buffer
 *   static char placeholder_buf[FILE_PATH_MAX]; // VexHome_placeholder buffer
 *   static char cache_buf[FILE_PATH_MAX];       // VexHome_cache buffer
 *   static char cache_index_buf[FILE_PATH_MAX]; // VexHome_cacheIndex buffer
 *   static bool ensured;                        // VexHome_ensure idempotency flag
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - VexHome_root(void)
 *   - VexHome_projects(void)
 *   - VexHome_logs(void)
 *   - VexHome_fonts(void)
 *   - VexHome_placeholder(void)
 *   - VexHome_cache(subsystem)
 *   - VexHome_cacheIndex(subsystem)
 *   - VexHome_cacheEnsure(subsystem)
 *   - VexHome_ensure(void)
 *   - VexHome_defaultLogPath(void)
 * ============================================================================
 */

// vexhome.c — VexHome port (was AntiHome; Legacy: io/AntiHome.java).
// Root resolved per-platform; legacy ~/vex and ~/anti never migrated or deleted.

#define ROOT_NAME      "vex"
#define PROJECTS_NAME  "projects"
#define LOGS_NAME      "logs"
#define FONTS_NAME     "fonts"
#define PLACEHOLDER    "placeholder"
#define CACHE_NAME     "cache"
#define LOG_FILE       "engine.bin"
#define INDEX_FILE     "dictionary.ini"

static char root_buf[FILE_PATH_MAX];
static char projects_buf[FILE_PATH_MAX];
static char logs_buf[FILE_PATH_MAX];
static char fonts_buf[FILE_PATH_MAX];
static char placeholder_buf[FILE_PATH_MAX];
static char cache_buf[FILE_PATH_MAX];
static char cache_index_buf[FILE_PATH_MAX];
static bool ensured;

/** Resolve the per-user home root using the platform-specific precedence below. */
static void resolve_base(char *out) {
    // 1. $VEX_HOME override (test seam)
    const char *vex_home = getenv("VEX_HOME");
    if (vex_home && *vex_home != '\0') {
        snprintf(out, FILE_PATH_MAX, "%s", vex_home);
        return;
    }

    const char *home = getenv("HOME");
    if (!home || *home == '\0')
        home = ".";

#if defined(VEX_PLATFORM_APPLE)
    // 2. macOS: ~/Library/Application Support/vexgraph
    snprintf(out, FILE_PATH_MAX, "%s/Library/Application Support/vexgraph", home);
#elif defined(VEX_PLATFORM_WINDOWS)
    // 3. Windows: %LOCALAPPDATA%\vexgraph (fallback %USERPROFILE%\vexgraph)
    const char *localappdata = getenv("LOCALAPPDATA");
    if (localappdata && *localappdata != '\0') {
        snprintf(out, FILE_PATH_MAX, "%s\\vexgraph", localappdata);
    } else {
        const char *userprofile = getenv("USERPROFILE");
        if (userprofile && *userprofile != '\0')
            snprintf(out, FILE_PATH_MAX, "%s\\vexgraph", userprofile);
        else
            snprintf(out, FILE_PATH_MAX, "%s/%s", home, ROOT_NAME);
    }
#else
    // 4. Linux/other: $XDG_DATA_HOME/vexgraph (fallback $HOME/.local/share/vexgraph)
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home && *xdg_data_home != '\0') {
        snprintf(out, FILE_PATH_MAX, "%s/vexgraph", xdg_data_home);
    } else {
        snprintf(out, FILE_PATH_MAX, "%s/.local/share/vexgraph", home);
    }
#endif

    // 5. Final fallback (handled by the else branches above using ROOT_NAME)
    // ROOT_NAME "vex" is now used ONLY for the final fallback when canonical
    // base is unavailable (e.g. no HOME on non-macOS/non-Windows).
}

/** Resolve the home root and append a nonempty relative subdirectory name. */
static void build_path(char *out, const char *sub) {
    resolve_base(out);
    if (*sub != '\0') {
        size_t len = strlen(out);
        snprintf(out + len, FILE_PATH_MAX - len, "/%s", sub);
    }
}

/** Resolve the cache root and append cache plus an optional subsystem component. */
static void build_cache_path(char *out, const char *subsystem) {
    resolve_base(out);
    size_t len = strlen(out);
    if (!subsystem || *subsystem == '\0')
        snprintf(out + len, FILE_PATH_MAX - len, "/%s", CACHE_NAME);
    else
        snprintf(out + len, FILE_PATH_MAX - len, "/%s/%s", CACHE_NAME, subsystem);
}

/** Return the resolved per-user engine home directory in static storage. */
const char *VexHome_root(void) {
    build_path(root_buf, "");
    return root_buf;
}

/** Return the projects directory path in static storage. */
const char *VexHome_projects(void) {
    build_path(projects_buf, PROJECTS_NAME);
    return projects_buf;
}

/** Return the logs directory path in static storage. */
const char *VexHome_logs(void) {
    build_path(logs_buf, LOGS_NAME);
    return logs_buf;
}

/** Return the fonts directory path in static storage. */
const char *VexHome_fonts(void) {
    build_path(fonts_buf, FONTS_NAME);
    return fonts_buf;
}

/** Return the placeholder-assets directory path in static storage. */
const char *VexHome_placeholder(void) {
    build_path(placeholder_buf, PLACEHOLDER);
    return placeholder_buf;
}

/** Return the cache directory path for a subsystem, or the shared cache root when null/empty. */
const char *VexHome_cache(const char *subsystem) {
    build_cache_path(cache_buf, subsystem);
    return cache_buf;
}

/** Return the subsystem cache's dictionary.ini path in static storage. */
const char *VexHome_cacheIndex(const char *subsystem) {
    const char *cache_dir = VexHome_cache(subsystem);
    snprintf(cache_index_buf, FILE_PATH_MAX, "%s/%s", cache_dir, INDEX_FILE);
    return cache_index_buf;
}

/** Ensure the subsystem cache directory and create its index only when absent. */
bool VexHome_cacheEnsure(const char *subsystem) {
    const char *cache_dir = VexHome_cache(subsystem);
    if (!File_mkdirs(cache_dir))
        return false;

    const char *index_path = VexHome_cacheIndex(subsystem);
    if (File_exists(index_path))
        return true;

    File *f = File_open(index_path, FILE_MODE_WRITE | FILE_MODE_CREATE | FILE_MODE_TRUNCATE);
    if (!f)
        return false;

    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char date_buf[32];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_buf);

    const char *sub = subsystem ? subsystem : "";
    char content[256];
    snprintf(content, sizeof(content),
             "# vexgraph cache index — created by VexHome_cacheEnsure\n"
             "[cache]\n"
             "subsystem = %s\n"
             "created = %s\n"
             "version = 1\n",
             sub, date_buf);

    int64_t written = File_write(f, content, (int64_t)strlen(content));
    bool ok = (written == (int64_t)strlen(content));
    File_close(f);
    return ok;
}

/** Idempotently create the root and standard projects, logs, fonts, placeholder, and cache directories. */
bool VexHome_ensure(void) {
    if (ensured)
        return true;
    if (!File_mkdirs(VexHome_root()))
        return false;
    if (!File_mkdirs(VexHome_projects()))
        return false;
    if (!File_mkdirs(VexHome_logs()))
        return false;
    if (!File_mkdirs(VexHome_fonts()))
        return false;
    if (!File_mkdirs(VexHome_placeholder()))
        return false;
    if (!File_mkdirs(VexHome_cache(nullptr)))
        return false;
    ensured = true;
    return true;
}

/** Ensure the home layout and return the engine log path within its logs directory. */
const char *VexHome_defaultLogPath(void) {
    VexHome_ensure();
    snprintf(logs_buf, FILE_PATH_MAX, "%s/%s", VexHome_logs(), LOG_FILE);
    return logs_buf;
}
