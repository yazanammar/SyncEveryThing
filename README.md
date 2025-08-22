# SyncEveryThing

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A lightweight, reliable C++17 command-line tool to synchronize files and directories. Designed for vault-style backups and fast content-aware syncs with dry-run, mirror, and ignore-list support.

---

## Key features

* Directory sync mode (`--dir`) and single-file sync (`--file`).
* Dry-run preview (`--dry-run`) — shows what would happen without making changes.
* Mirror mode (`--delete`) — removes destination items not present in source.
* Source-based ignore paths (`--ignore`, repeatable).
* Fast dedupe/move heuristics when identical content exists in destination.
* Default fingerprinting: FNV64 (fast). Optional Windows SHA-256 using CNG (`--sha256`), with new granular controls to apply it only to files within a specific size range (`--sha256-min`, `--sha256-max`).* Optional colored output, logging to `sync.log`, and saving runtime settings to `settings.json`.
* Windows helper to add the tool folder to the current user's PATH (`--add-to-path`).

---

## Repository layout

```
/ (repo root)
  ├─ sync.cpp             # single-source-file implementation
  ├─ README.md            # this file
  ├─ LICENSE              # recommended: MIT
  └─ .github/workflows    # optional CI GitHub Actions
```

---

## Building

**Requirements**

* C++17-capable compiler (g++, clang, MSVC)
* Standard C++ library with `<filesystem>` support

**Notes about Windows-only features**

* `--sha256` uses Windows CNG (BCrypt) and is implemented only when building on Windows with the Windows SDK and linking `bcrypt.lib`.
* `--add-to-path` manipulates the current user's registry and is Windows-only.

**Suggested commands**

Windows (MSVC, recommended if you want `--sha256`):

```powershell
cl /std:c++17 sync.cpp /link bcrypt.lib /Fe:sync.exe
```

Windows (MinGW/GCC — if libbcrypt is available):

```bash
g++ -std=c++17 sync.cpp -o sync -lbcrypt
```

Linux / macOS (POSIX builds):

```bash
g++ -std=c++17 sync.cpp -o sync-everything
# Do not use --sha256 on POSIX builds unless you add a cross-platform SHA-256 implementation.
```

**Important compile-time fix**

* The source needs `#include <cstdint>` to ensure fixed-width integer types (`uint8_t`, `uint64_t`) are defined on all compilers. Make sure this line is present at the top of `SyncEveryThing.cpp`.

---

## Usage

```
.\sync.exe --dir <source_directory> <dest_directory> [options]
.\sync.exe --file <source_file> <dest_directory> [options]
```

Options:

```
--dir <path>        Sync a directory (vault mode)
--file <path>       Sync a single file
--ignore <path>     Ignore a path (repeatable)  <-- paths are SOURCE paths
--delete            Mirror mode: delete dest items missing in source
--dry-run           Show operations without applying changes
--verbose           Verbose output (shows logs)
--color             Colored output
--save-log          Save operations to sync.log
--save-settings     Save arguments to settings.json
--sha256            Use SHA-256 (Windows CNG) for fingerprints (Windows only)
--sha256-min <N>    Minimum file size to use SHA-256 (e.g. 1M, 500K)
--sha256-max <N>    Maximum file size to use SHA-256 (e.g. 500M, 2G)
--add-to-path       [Windows] add tool to user PATH
-h, --help          Show help

```

---

## Examples

Dry-run sync a vault (Unix example):

```bash
./sync --dir "/home/alex/Vault" "/mnt/backup/Vault" --ignore "/home/alex/Vault/.git" --dry-run --verbose --color
```

Mirror mode (Windows example — be careful):

```powershell
sync.exe --dir "C:\Users\me\Vault" "D:\Backup\Vault" --delete --save-log --verbose --color
```

Single file sync:

```bash
./sync --file "/home/alex/.config/nvim/init.vim" "/mnt/config-backup" --dry-run
```

Run with saved settings (no args):

* If `settings.json` is present and `--save-settings` was used previously, the program loads settings and runs the last-saved operation.

---

## Implementation notes & important fixes applied

During review of the source code, the following fixes and clarifications were applied or must be present before publishing:

1. **Add `#include <cstdint>`**

   * Ensures `uint8_t`, `uint64_t` types compile cleanly on all toolchains.

2. **Fix duplicated `std::error_code` block (copy/paste bug)**

   * Two places in the original source re-declared `ec1`, `ec2`, `ssz`, and `tsz` inside nested blocks, causing shadowing and logic errors. The corrected logic:

     * Query file sizes with `std::error_code` variables.
     * If `file_size()` reports an error, fall back to `last_write_time()` comparison.
     * If sizes differ, set `needCopy = true`.
     * If sizes equal, compute fingerprints and compare.
   * This fix exists in both `syncDir()` and `syncFile()` paths where the bug appeared.

3. **Registry PATH read when value missing** (Windows `addToPath`)

   * `RegQueryValueExW` can return `ERROR_FILE_NOT_FOUND`. Avoid copying an uninitialized buffer — check the return and set `currentPath` appropriately. On unexpected errors, close the key and fail gracefully.

4. **Windows-only features explicitly documented**

   * `--sha256` and `--add-to-path` are documented as Windows-only. If a user attempts `--sha256` on non-Windows builds, the program should print a clear message and ignore the flag.

5. **Robustness suggestions (not mandatory but recommended)**

   * Limit concurrent copy tasks (e.g., a semaphore or thread-pool) to avoid IO saturation when syncing thousands of files.
   * Catch and handle `std::filesystem` exceptions from iterators (permissions, unreadable paths).
   * Consider using a `std::deque<std::future<void>>` and moving futures to avoid accidental copies and make lifecycle management clearer.
   * Add signal handling (SIGINT) to cancel and join outstanding futures gracefully.

---

## Fingerprinting: FNV64 vs SHA-256 (summary)

* **FNV64 (default)**

  * Fast. For large files, the implementation reads the first and last 128KB and computes a lightweight FNV-like hash. Good for speed and dedupe heuristics.
  * Not cryptographically collision-proof (but suitable for most dedupe tasks).

* **SHA-256 (`--sha256`)**

  * Uses Windows CNG (BCrypt) when built on Windows. Stronger, safer, but slower and causes more disk IO.
  * Only available on Windows in the current implementation.

**Recommendation:** Keep default FNV64 for routine runs. Use `--sha256` when you need the highest integrity guarantee and you are on Windows.

---

## Safety notes & mirror mode

Mirror mode (`--delete`) will remove files and directories from the destination that are not present in source (subject to ignore rules). **Always run with `--dry-run` first** when using mirror mode for a new job to ensure no unintended deletions.

---

## Changelog

* **v1.0** — Initial public release: directory & file sync, FNV64 fingerprints, dry-run, mirror, ignore, colored output, Windows SHA-256 support.
* **v1.1** — Fix: file size check duplication bug; added `<cstdint>` include; improved registry PATH handling; recommendations for concurrency throttling.
* * **v1.2** — New: Added `--sha256-min` and `--sha256-max` for size-based conditional SHA-256 fingerprinting. Changed default `--sha256` behavior to apply to all files unless limits are specified.

---

## Using as a Library

While SyncEveryThing is designed as a standalone command-line tool, its core synchronization logic can be integrated into your own C++ projects.

The tool is provided as a single source file (`sync.cpp`) and does not have a separate header file. To use its functions:

1.  Copy the `sync.cpp` file into your project's source directory.
2.  Remove or comment out the `main()` function to avoid a linking conflict.
3.  You can then call the core functions, such as `syncDir()` and `syncFile()`, directly from your code by providing the necessary parameters.
4.  Ensure your project is compiled with C++17 support and links the necessary libraries (e.g., `bcrypt.lib` on Windows if using SHA-256 functionality).

**Example Usage:**

```cpp
// In your own project's code:
// Note: You would need to include the necessary headers like <filesystem>, <vector>, etc.

// Forward declare the function you want to use from sync.cpp
void syncDir(const std::filesystem::path& src, const std::filesystem::path& dst, const std::vector<std::filesystem::path>& ignorePaths,
             bool dryRun, bool verbose, bool mirror, bool enableColors);

void myCustomBackupFunction() {
    fs::path source = "C:\\my-data";
    fs::path destination = "D:\\my-backup";
    std::vector<fs::path> ignore_list;
    ignore_list.push_back(source / ".cache");

    // Call the core logic directly
    syncDir(source, destination, ignore_list, false, true, true, true);
}
```
---

## Contributing

* Fork the repo, open PRs for fixes and features.
* Add unit tests for helper functions (normalization, fingerprinting) where possible.
* Consider adding cross-platform SHA-256 (OpenSSL, libsodium) if you want `--sha256` on POSIX.

---

## Support & Contributions

If you encounter any issues or have a feature request, please don't hesitate to [open an issue](https://github.com/yazanammar/SyncEveryThing/issues) on our GitHub repository. We welcome all feedback and contributions!
