# Changelog

All notable changes to this project will be documented in this file.

## v1.0.3 - 2025-08-24

- **New:** Added performance control modes (`--ultra-speed`, `--minimum-speed`) to manage process priority and the number of simultaneous copy operations.
- **Improved:** Implemented a semaphore to control and limit concurrent copy tasks, preventing I/O saturation and improving stability on slower systems.

## v1.0.2 - 2025-08-24

- **New:** Added size-based conditional hashing with `--sha256-min` and `--sha256-max` flags, allowing SHA-256 to be used only for files within a specific size range.
- **Changed:** The default behavior of `--sha256` now applies the hash to all files unless a size range is specified.

## v1.0.1 - 2025-08-22

- **Fixed:** Corrected a critical logic bug where file size and `std::error_code` variables were re-declared, causing incorrect comparisons.
- **Fixed:** Improved the reliability of adding the tool to the Windows PATH by correctly handling cases where the registry value might not exist.
- **Improved:** Added the necessary `#include <cstdint>` for better cross-compiler compatibility.

## v1.0.0 - 2025-08-22

- **Initial Release:** First public version of SyncEveryThing.
- **Feature:** Core synchronization logic for both directories (`--dir`) and single files (`--file`).
- **Feature:** Fast FNV64 fingerprinting for quick content comparison and move detection.
- **Feature:** Windows-only SHA-256 support for high-integrity checks.
- **Feature:** Essential utilities including `--dry-run`, `--delete` (mirror mode), `--ignore`, and colored console output.
