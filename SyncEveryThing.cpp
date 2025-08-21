// Compile: (Windows) cl /std:c++17 test.cpp /link bcrypt.lib
//          (GCC)     g++ -std=c++17 test.cpp -lbcrypt -o SHA256.exe

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <future>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <ntstatus.h>
#pragma comment(lib, "Bcrypt")
#endif

namespace fs = std::filesystem;

// ========== ANSI Color Codes ==========

// --- Normal colors ---

const std::string RESET = "\033[0m";
const std::string BLACK = "\033[30m";
const std::string RED = "\033[31m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string BLUE = "\033[34m";
const std::string MAGENTA = "\033[35m"; // Purple
const std::string CYAN = "\033[36m"; 
const std::string WHITE = "\033[37m";
const std::string PURPLE = "\e[0;35m" ;

// --- Bold/Bright colors (Bold/Bright) ---

const std::string BOLD_RED = "\033[1;31m";
const std::string BOLD_GREEN = "\033[1;32m";
const std::string BOLD_YELLOW = "\033[1;33m";
const std::string BOLD_BLUE = "\033[1;34m";
const std::string BOLD_MAGENTA = "\033[1;35m";
const std::string BOLD_CYAN = "\033[1;36m";
const std::string BOLD_WHITE = "\033[1;37m";

// ========== SHA-256 / fingerprint support ==========
static bool g_use_sha256 = false;

static std::string bytes_to_hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    return oss.str();
}

#ifdef _WIN32
static std::string compute_file_sha256_hex(const fs::path& path) {
    constexpr LPCWSTR ALG = BCRYPT_SHA256_ALGORITHM;
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();

    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, ALG, NULL, 0);
    if (!BCRYPT_SUCCESS(status) || hAlg == NULL) return std::string();

    DWORD cbHashObject = 0, cbData = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbHashObject, sizeof(cbHashObject), &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg,0); return std::string(); }

    std::vector<uint8_t> hashObject(cbHashObject);
    status = BCryptCreateHash(hAlg, &hHash, hashObject.data(), (ULONG)hashObject.size(), NULL, 0, 0);
    if (!BCRYPT_SUCCESS(status) || hHash == NULL) { BCryptCloseAlgorithmProvider(hAlg,0); return std::string(); }

    const size_t CHUNK = 1 << 16; // 64KB
    std::vector<uint8_t> buf(CHUNK);
    while (in.good()) {
        in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)buf.size());
        std::streamsize r = in.gcount();
        if (r > 0) {
            status = BCryptHashData(hHash, buf.data(), (ULONG)r, 0);
            if (!BCRYPT_SUCCESS(status)) { BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg,0); return std::string(); }
        }
    }

    DWORD cbHash = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash, sizeof(cbHash), &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptDestroyHash(hHash); BCryptCloseAlgorithmProvider(hAlg,0); return std::string(); }

    std::vector<uint8_t> hashBuf(cbHash);
    status = BCryptFinishHash(hHash, hashBuf.data(), (ULONG)hashBuf.size(), 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) return std::string();
    return bytes_to_hex(hashBuf.data(), hashBuf.size());
}
#endif

static std::string compute_file_fnv_hex(const fs::path& path) {
    const size_t CHUNK = 128 * 1024;
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::string();
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf;
    auto fnv64 = [](const uint8_t* data, size_t len)->uint64_t {
        const uint64_t FNV_OFFSET = 1469598103934665603ull;
        const uint64_t FNV_PRIME  = 1099511628211ull;
        uint64_t h = FNV_OFFSET;
        for (size_t i = 0; i < len; ++i) { h ^= data[i]; h *= FNV_PRIME; }
        return h;
    };

    if (size <= (std::streamoff)(2 * CHUNK)) {
        if (size == 0) return std::string();
        buf.resize((size_t)size);
        f.read(reinterpret_cast<char*>(buf.data()), size);
        uint64_t h = fnv64(buf.data(), buf.size());
        std::ostringstream oss; oss << std::hex << std::setfill('0') << std::setw(16) << h;
        return oss.str();
    } else {
        buf.resize(2 * CHUNK);
        f.read(reinterpret_cast<char*>(buf.data()), CHUNK);
        f.seekg(size - CHUNK, std::ios::beg);
        f.read(reinterpret_cast<char*>(buf.data()+CHUNK), CHUNK);
        uint64_t h = fnv64(buf.data(), buf.size());
        std::ostringstream oss; oss << std::hex << std::setfill('0') << std::setw(16) << h;
        return oss.str();
    }
}

static std::string file_fingerprint_hex(const fs::path& p) {
    if (g_use_sha256) {
#ifdef _WIN32
        std::string hex = compute_file_sha256_hex(p);
        if (!hex.empty()) return hex;
#endif
    }
    return compute_file_fnv_hex(p);
}

// ========== Logging ==========
std::mutex logMutex;
std::ofstream logFile;

void logMsg(const std::string& msg, bool verbose, bool useColor) {
    { 
        std::lock_guard<std::mutex> lock(logMutex);
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            logFile << "[" << std::ctime(&now);
            logFile.seekp(-1, std::ios::cur);
            logFile << "] " << msg << "\n";
        }
    }

    if (verbose) {
        if (useColor) {
            std::string final_msg = msg;
            if (msg.find("[DRY-RUN]") != std::string::npos || msg.find("INFO:") != std::string::npos || msg.find("Would MOVE") != std::string::npos) {
                final_msg = BOLD_YELLOW + msg + RESET;
            } 
            else if (msg.find("SUCCESS!") != std::string::npos || msg.find("Copied") != std::string::npos || msg.find("All Tasks Finished !!") != std::string::npos || msg.find("Renamed") != std::string::npos || msg.find("Deleted:") != std::string::npos) {
                final_msg = BOLD_GREEN + msg + RESET;
            }
            else if (msg.find("[X] ERROR:") != std::string::npos) {
                final_msg = BOLD_RED + msg + RESET;
            }
            else if (msg.find("Ignored:") != std::string::npos) {
                final_msg = BLUE + msg + RESET;
            }
            std::cout << final_msg << std::endl;
        } else {
            std::cout << msg << std::endl;
        }
    }
}

// ========== Settings helper ==========
const std::string SETTINGS_FILE = "settings.json";
void saveSettings(const std::map<std::string, std::string>& settings) {
    std::ofstream out(SETTINGS_FILE);
    out << "{\n";
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        out << "  \"" << it->first << "\": \"" << it->second << "\"";
        if (std::next(it) != settings.end()) out << ",";
        out << "\n";
    }
    out << "}\n";
}
std::map<std::string, std::string> loadSettings() {
    std::map<std::string, std::string> settings;
    std::ifstream in(SETTINGS_FILE);
    if (!in.is_open()) return settings;
    std::string line;
    while (std::getline(in, line)) {
        auto keyStart = line.find("\"");
        auto keyEnd = line.find("\"", keyStart + 1);
        if (keyStart == std::string::npos || keyEnd == std::string::npos) continue;
        std::string key = line.substr(keyStart + 1, keyEnd - keyStart - 1);
        auto valStart = line.find("\"", keyEnd + 1);
        auto valEnd = line.find("\"", valStart + 1);
        if (valStart == std::string::npos || valEnd == std::string::npos) continue;
        std::string val = line.substr(valStart + 1, valEnd - valStart - 1);
        settings[key] = val;
    }
    return settings;
}

// ========== Copy helper ==========
std::future<void> copyFileAsync(const fs::path& src, const fs::path& dst, bool dryRun, bool verbose, bool enableColors) {
    if (dryRun) {
        if (fs::exists(dst)) {
            logMsg("[DRY-RUN] Would DELETE and then COPY " + src.string() + " -> " + dst.string(), true, enableColors);
        } else {
            logMsg("[DRY-RUN] Would copy " + src.string() + " -> " + dst.string(), true, enableColors);
        }
        return std::async(std::launch::deferred, [](){});
    }

    if (!fs::exists(dst.parent_path())) {
        fs::create_directories(dst.parent_path());
    }

    return std::async(std::launch::async, [=]() {
        try {
            if (fs::exists(dst)) {
                fs::remove(dst);
            }
            fs::copy_file(src, dst); 
            logMsg("Copied " + src.string() + " -> " + dst.string(), true, enableColors);
        } catch (const std::exception& ex) {
            logMsg(std::string("[X] ERROR copying file: ") + ex.what() + " [" + src.string() + "] [" + dst.string() + "]", true, enableColors);
            throw;
        }
    });
}

// ========== Normalization utilities ==========
static std::string normalize_generic(const fs::path& p) {
    std::string s = p.generic_string();
#ifdef _WIN32
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
#endif
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}
inline bool same_or_child_of_norm(const std::string& dirPrefix, const std::string& path) {
    if (path == dirPrefix) return true;
    if (dirPrefix.empty()) return false;
    if (path.size() > dirPrefix.size() && path.rfind(dirPrefix + "/", 0) == 0) return true;
    return false;
}
inline bool is_reserved_path_norm(const std::unordered_set<std::string>& reserved_dirs,
                                  const std::unordered_set<std::string>& reserved_paths,
                                  const fs::path& p) {
    std::string s = normalize_generic(p);
    if (reserved_paths.find(s) != reserved_paths.end()) return true;
    for (const auto& d : reserved_dirs) {
        if (same_or_child_of_norm(d, s)) return true;
    }
    return false;
}

// ========== Ignore helpers ==========
static bool path_is_under_any_ignore(const std::vector<fs::path>& ignorePaths, const fs::path& candidate) {
    std::string cand_norm = normalize_generic(candidate);
    for (const auto& ig : ignorePaths) {
        std::string ig_norm = normalize_generic(ig);
        if (ig_norm.empty()) continue;
        if (cand_norm == ig_norm) return true;
        if (cand_norm.size() > ig_norm.size() && cand_norm.rfind(ig_norm + "/", 0) == 0) return true;
    }
    return false;
}

static bool dst_entry_src_is_ignored(const std::vector<fs::path>& ignorePaths, const fs::path& dstRoot, const fs::path& dstEntry, const fs::path& srcRoot) {
    std::error_code ec;
    fs::path rel = fs::relative(dstEntry, dstRoot, ec);
    if (ec) return false;
    fs::path srcEquivalent = srcRoot / rel;
    return path_is_under_any_ignore(ignorePaths, srcEquivalent);
}

// ========== Helper: matchIgnore (source-based) ==========
bool matchIgnore(const std::vector<fs::path>& ignorePaths, const fs::path& currentEntry) {
    if (!fs::exists(currentEntry)) return false;
    return path_is_under_any_ignore(ignorePaths, currentEntry);
}

void syncDir(const fs::path& src, const fs::path& dst, const std::vector<fs::path>& ignorePaths,
             bool dryRun, bool verbose, bool mirror, bool enableColors) {

    if (!fs::exists(src)) {
        logMsg("Source does not exist: " + src.string(), true, enableColors);
        return;
    }
    if (!dryRun) fs::create_directories(dst);
    else if (!fs::exists(dst)) logMsg("[DRY-RUN] Would create directory " + dst.string(), true, enableColors);

    std::unordered_multimap<std::string, fs::path> dst_fp_map;
    if (g_use_sha256) {
        logMsg("[INFO] Building destination fingerprint index (this may take some time)...", verbose, enableColors);
        for (const auto& e : fs::recursive_directory_iterator(dst)) {
            if (!e.is_regular_file()) continue;
            if (dst_entry_src_is_ignored(ignorePaths, dst, e.path(), src)) continue;
            std::string f = file_fingerprint_hex(e.path());
            if (!f.empty()) dst_fp_map.emplace(f, e.path());
        }
        logMsg("[INFO] Destination fingerprint index ready (" + std::to_string(dst_fp_map.size()) + " entries).", verbose, enableColors);
    }

    std::unordered_map<std::string, std::unordered_set<std::string>> dir_fp_cache;
    auto collect_dir_fps = [&](const fs::path& dir)->std::unordered_set<std::string> {
        std::string key = normalize_generic(dir);
        auto itc = dir_fp_cache.find(key);
        if (itc != dir_fp_cache.end()) return itc->second;
        std::unordered_set<std::string> s;
        if (!fs::exists(dir)) { dir_fp_cache.emplace(key, s); return s; }
        for (const auto& f : fs::recursive_directory_iterator(dir)) {
            if (!f.is_regular_file()) continue;
            if (matchIgnore(ignorePaths, f.path())) continue;
            std::string fp = file_fingerprint_hex(f.path());
            if (!fp.empty()) s.insert(fp);
        }
        dir_fp_cache.emplace(key, s);
        return s;
    };

    std::unordered_set<std::string> reserved_dirs; 
    std::unordered_set<std::string> reserved_paths; 

    std::vector<fs::path> moved_src_roots;
    std::vector<std::future<void>> copyTasks;
    int operations_count = 0;

    for (auto it = fs::recursive_directory_iterator(src); it != fs::recursive_directory_iterator(); ++it) {
        const auto& entry = *it;
        fs::path rel = fs::relative(entry.path(), src);
        fs::path target = dst / rel;

        bool under_moved = false;
        for (const auto& mr : moved_src_roots) {
            if (entry.path().string().rfind(mr.generic_string(), 0) == 0) { under_moved = true; break; }
        }
        if (under_moved) {
            if (entry.is_directory()) it.disable_recursion_pending();
            continue;
        }

        if (matchIgnore(ignorePaths, entry.path())) {
            logMsg("Ignored: " + entry.path().string(), verbose || dryRun, enableColors);
            if (entry.is_directory()) it.disable_recursion_pending();
            continue;
        }

        if (entry.is_directory()) {
            if (!fs::exists(target)) {
                bool didDirMove = false;
                if (g_use_sha256) {
                    auto src_fps = collect_dir_fps(entry.path());
                    if (!src_fps.empty()) {
                        fs::path dst_parent = dst / rel.parent_path();
                        if (fs::exists(dst_parent) && fs::is_directory(dst_parent)) {
                            for (const auto& cand : fs::directory_iterator(dst_parent)) {
                                if (!cand.is_directory()) continue;
                                fs::path cand_path = cand.path();
                                std::string cand_norm = normalize_generic(cand_path);
                                if (reserved_dirs.find(cand_norm) != reserved_dirs.end()) continue;
                                if (dst_entry_src_is_ignored(ignorePaths, dst, cand_path, src)) continue;
                                auto cand_fps = collect_dir_fps(cand_path);
                                if (cand_fps.empty()) continue;
                                size_t common = 0;
                                for (const auto& fp : src_fps) if (cand_fps.find(fp) != cand_fps.end()) ++common;
                                double ratio = src_fps.empty() ? 0.0 : (double)common / (double)src_fps.size();
                                if (ratio >= 0.85) {
                                    if (dryRun) {
                                        logMsg("[DRY-RUN] Would MOVE (rename dir) " + cand_path.string() + " -> " + target.string(), true, enableColors);
                                        reserved_dirs.insert(normalize_generic(cand_path));
                                        reserved_dirs.insert(normalize_generic(target));
                                        moved_src_roots.push_back(entry.path());
                                        didDirMove = true;
                                        operations_count++;
                                        break;
                                    } else {
                                        try {
                                            if (!fs::exists(target.parent_path())) fs::create_directories(target.parent_path());
                                            std::error_code ec;
                                            fs::rename(cand_path, target, ec);
                                            if (!ec) {
                                                logMsg(std::string("[INFO] Renamed directory ") + cand_path.string() + " -> " + target.string(), true, enableColors);
                                                reserved_dirs.insert(normalize_generic(target));
                                            } else {
                                                for (auto& e2 : fs::recursive_directory_iterator(cand_path)) {
                                                    if (!e2.is_regular_file()) continue;
                                                    fs::path rel2 = fs::relative(e2.path(), cand_path);
                                                    fs::path dest2 = target / rel2;
                                                    if (!fs::exists(dest2.parent_path())) fs::create_directories(dest2.parent_path());
                                                    fs::copy_file(e2.path(), dest2, fs::copy_options::overwrite_existing);
                                                }
                                                fs::remove_all(cand_path);
                                                logMsg(std::string("[INFO] Copied directory ") + cand_path.string() + " -> " + target.string() + " (cross-volume move)", true, enableColors);
                                                logMsg(std::string("[INFO] Deleted original ") + cand_path.string(), true, enableColors);
                                                reserved_dirs.insert(normalize_generic(target));
                                            }
                                            moved_src_roots.push_back(entry.path());
                                            for (auto dit = dst_fp_map.begin(); dit != dst_fp_map.end(); ) {
                                                if (dit->second.string().rfind(cand_path.string(), 0) == 0) dit = dst_fp_map.erase(dit);
                                                else ++dit;
                                            }
                                            didDirMove = true;
                                        } catch (const std::exception& ex) {
                                            logMsg(std::string("[X] ERROR moving directory: ") + ex.what(), true, enableColors);
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }

                if (didDirMove) {
                    if (entry.is_directory()) it.disable_recursion_pending();
                    continue;
                }

                // create directory normally (reserve exact path)
                if (dryRun) {
                    logMsg("[DRY-RUN] Would create directory " + target.string(), true, enableColors);
                    operations_count++;
                    reserved_paths.insert(normalize_generic(target));
                } else {
                    fs::create_directories(target);
                    logMsg("Create Directory " + target.string(), true, enableColors);
                    reserved_paths.insert(normalize_generic(target));
                }
            } else {
            }
            continue;
        }

        bool needCopy = false;
        if (!fs::exists(target)) {
            bool moved = false;
            if (g_use_sha256 && !dst_fp_map.empty()) {
                std::string sfp = file_fingerprint_hex(entry.path());
                if (!sfp.empty()) {
                    auto range = dst_fp_map.equal_range(sfp);
                    for (auto dit = range.first; dit != range.second; ++dit) {
                        fs::path candidate = dit->second;
                        if (dst_entry_src_is_ignored(ignorePaths, dst, candidate, src)) continue;
                        std::string cand_norm = normalize_generic(candidate);
                        if (reserved_paths.find(cand_norm) != reserved_paths.end()) continue;
                        if (!fs::exists(candidate)) continue;
                        if (dryRun) {
                            logMsg("[DRY-RUN] Would MOVE (rename) " + candidate.string() + " -> " + target.string(), true, enableColors);
                            reserved_paths.insert(normalize_generic(candidate));
                            reserved_paths.insert(normalize_generic(target));
                            moved = true; operations_count++; break;
                        } else {
                            try {
                                if (!fs::exists(target.parent_path())) fs::create_directories(target.parent_path());
                                std::error_code ec;
                                fs::rename(candidate, target, ec);
                                if (!ec) {
                                    logMsg(std::string("[INFO] Renamed file ") + candidate.string() + " -> " + target.string(), true, enableColors);
                                } else {
                                    fs::copy_file(candidate, target, fs::copy_options::overwrite_existing);
                                    fs::remove(candidate);
                                    logMsg(std::string("[INFO] Copied file ") + candidate.string() + " -> " + target.string() + " (cross-volume move)", true, enableColors);
                                    logMsg(std::string("[INFO] Deleted original ") + candidate.string(), true, enableColors);
                                }
                                auto fr = dst_fp_map.equal_range(sfp);
                                for (auto eit = fr.first; eit != fr.second; ++eit) {
                                    if (eit->second == candidate) { dst_fp_map.erase(eit); break; }
                                }
                                reserved_paths.insert(normalize_generic(target));
                                moved = true;
                            } catch (const std::exception& ex) {
                                logMsg(std::string("[X] ERROR moving file: ") + ex.what(), true, enableColors);
                            }
                            break;
                        }
                    }
                }
            }
            if (!moved) needCopy = true;
        } else {
            if (g_use_sha256) {
                std::error_code ec1, ec2;
                uintmax_t ssz = fs::file_size(entry.path(), ec1);
                uintmax_t tsz = fs::file_size(target, ec2);
                if (ec1 || ec2) {
                std::error_code ec1, ec2;
                uintmax_t ssz = fs::file_size(entry.path(), ec1);
                uintmax_t tsz = fs::file_size(target, ec2);
                if (ec1 || ec2 || ssz != tsz || fs::last_write_time(entry.path()) > fs::last_write_time(target)) {
                    needCopy = true;
                }
                } else if (ssz != tsz) {
                    needCopy = true;
                } else {
                    std::string sh = file_fingerprint_hex(entry.path());
                    std::string dh = file_fingerprint_hex(target);
                    if (sh.empty() || dh.empty() || sh != dh) needCopy = true;
                }
            } else {
                if (fs::last_write_time(entry.path()) > fs::last_write_time(target)) needCopy = true;
            }
            reserved_paths.insert(normalize_generic(target)); 
        }

        if (needCopy) {
            if (dryRun) {
                logMsg("[DRY-RUN] Would copy " + entry.path().string() + " -> " + target.string(), true, enableColors);
                operations_count++;
                reserved_paths.insert(normalize_generic(target));
            } else {
                reserved_paths.insert(normalize_generic(target));
                copyTasks.push_back(copyFileAsync(entry.path(), target, dryRun, verbose, enableColors));
            }
        }
    }

    if (mirror) {
        logMsg("\nMirror mode enabled. Checking for files to delete from destination...", verbose, enableColors);
        std::vector<fs::path> pathsToDelete;
        for (const auto& entry : fs::recursive_directory_iterator(dst)) {
            if (is_reserved_path_norm(reserved_dirs, reserved_paths, entry.path())) continue;
            if (dst_entry_src_is_ignored(ignorePaths, dst, entry.path(), src)) continue;
            fs::path srcPath = src / fs::relative(entry.path(), dst);
            if (!fs::exists(srcPath) && !matchIgnore(ignorePaths, srcPath)) {
                pathsToDelete.push_back(entry.path());
            }
        }
        if (!pathsToDelete.empty()) {
            if (dryRun) operations_count += (int)pathsToDelete.size();
            std::sort(pathsToDelete.rbegin(), pathsToDelete.rend());
            for (const auto& p : pathsToDelete) {
                if (dryRun) logMsg("[DRY-RUN] Would delete " + p.string(), true, enableColors);
                else { if (fs::exists(p)) { fs::remove_all(p); logMsg("Deleted: " + p.string(), true, enableColors); } }
            }
        }
    }

    if (!dryRun && !copyTasks.empty()) {
        logMsg("Waiting for all copy tasks to complete...", true, enableColors);
        for (auto& t : copyTasks) { try { t.get(); } catch (const std::exception& ex) { logMsg(std::string("[X] COPY TASK ERROR: ") + ex.what(), true, enableColors); } catch (...) { logMsg("[X] COPY TASK ERROR (unknown)", true, enableColors); } }
    }

    logMsg("\nAll Tasks Finished !!", verbose || dryRun, enableColors);
    if (dryRun && operations_count == 0) {
        logMsg("\n========================================", true, enableColors);
        logMsg("==> [DRY-RUN] Source and destination are already in sync. No changes needed.", true, enableColors);
        logMsg("========================================", true, enableColors);
    }
}

// ========== File sync ==========
void syncFile(const fs::path& src, const fs::path& dst, bool dryRun, bool verbose, bool enableColors) {
    if (!fs::exists(src)) { logMsg("Source file missing: " + src.string(), true, enableColors); return; }
    if (!fs::exists(dst) && !dryRun) fs::create_directories(dst);
    auto target = dst / src.filename();
    bool needCopy = false;
    if (!fs::exists(target)) needCopy = true;
    else {
        if (g_use_sha256) {
            std::error_code ec1, ec2;
            uintmax_t ssz = fs::file_size(src, ec1);
            uintmax_t tsz = fs::file_size(target, ec2);
            if (ec1 || ec2) { if (fs::last_write_time(src) > fs::last_write_time(target)) needCopy = true; }
            else if (ssz != tsz) needCopy = true;
            else {
                std::string sh = file_fingerprint_hex(src);
                std::string dh = file_fingerprint_hex(target);
                if (sh.empty() || dh.empty() || sh != dh) needCopy = true;
            }
        } else {
        std::error_code ec1, ec2;
        uintmax_t ssz = fs::file_size(src, ec1);
        uintmax_t tsz = fs::file_size(target, ec2);
        if (ec1 || ec2 || ssz != tsz || fs::last_write_time(src) > fs::last_write_time(target)) {
            needCopy = true;
        }
        }
    }
    if (needCopy) {
        if (dryRun) logMsg("[DRY-RUN] Would copy " + src.string() + " -> " + target.string(), true, enableColors);
        else { auto fut = copyFileAsync(src, target, dryRun, verbose, enableColors); fut.get(); }
    }
}

// ========== Windows helpers ==========
#ifdef _WIN32
void enableVirtualTerminalProcessing() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) return;
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}
void addToPath(const fs::path& exePath, bool verbose, bool enableColors) {
    logMsg("Attempting to add tool to user PATH...", true, enableColors);
    fs::path dirPath = exePath.parent_path();
    std::wstring dirPathW = dirPath.wstring();
    HKEY hKey;
    LONG lResult = RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ | KEY_WRITE, &hKey);
    if (lResult != ERROR_SUCCESS) { logMsg("[X] ERROR: Could not open Registry key.", true, enableColors); return; }
    WCHAR szBuffer[32767]; DWORD dwBufferSize = sizeof(szBuffer);
    lResult = RegQueryValueExW(hKey, L"Path", 0, NULL, (LPBYTE)szBuffer, &dwBufferSize);
    if (lResult != ERROR_SUCCESS && lResult != ERROR_FILE_NOT_FOUND) { RegCloseKey(hKey); logMsg("[X] ERROR: Could not read PATH.", true, enableColors); return; }
    std::wstring currentPath = szBuffer;
    if (currentPath.find(dirPathW) != std::wstring::npos) { logMsg("[*] INFO: Path already present.", true, enableColors); RegCloseKey(hKey); return; }
    std::wstring newPath = currentPath;
    if (!newPath.empty() && newPath.back() != L';') newPath += L';';
    newPath += dirPathW;
    lResult = RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ, (LPBYTE)newPath.c_str(), (newPath.length() + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);
    if (lResult != ERROR_SUCCESS) { logMsg("[X] ERROR: Could not write new PATH.", true, enableColors); return; }
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
    logMsg("[*] SUCCESS! : Tool directory added to user PATH.", true, enableColors);
    logMsg("==> Please open a NEW terminal for the changes to take effect.", true, enableColors);
}
#endif

// ========== Help ==========
void printHelp(const std::string& exeName) {
    std::cout << "Usage:\n"
              << "  " << exeName << " --dir <source_directory> <dest_directory> [options]\n"
              << "  " << exeName << " --file <source_file> <dest_directory> [options]\n\n"
              << "Options:\n"
              << "  --dir <path>        Sync a directory (vault mode)\n"
              << "  --file <path>       Sync a single file\n"
              << "  --ignore <path>     Ignore a path (repeatable)  <-- paths are SOURCE paths\n"
              << "  --delete            Mirror mode: delete dest items missing in source\n"
              << "  --dry-run           Show operations without applying changes\n"
              << "  --verbose           Verbose output (shows logs)\n"
              << "  --color             Colored output\n"
              << "  --save-log          Save operations to sync.log\n"
              << "  --save-settings     Save arguments to settings.json\n"
              << "  --sha256            Use SHA-256 (Windows CNG) for fingerprints\n"
#ifdef _WIN32
              << "  --add-to-path       [Windows] add tool to user PATH\n"
#endif
              << "  -h, --help          Show help\n";
}

// ========== Main ==========
int main(int argc, char* argv[]) {
#ifdef _WIN32
    enableVirtualTerminalProcessing();
#endif
    std::cout << PURPLE << "\n--- RUNNING NEW VERSION 16(fixed)\n" << RESET << std::endl;

    if (argc < 2) {
        auto loadedSettings = loadSettings();
        if (loadedSettings.empty()) { printHelp(fs::path(argv[0]).filename().string()); return 0; }
    }

    bool dryRun=false, verbose=false, saveSettingsFlag=false, mirror=false;
    bool saveLog=false, enableColors=false, useSha256=false;
    std::vector<fs::path> ignorePaths;
    std::map<std::string,std::string> settings;
    std::string mode; fs::path src, dst;

    for (int i=1;i<argc;i++) {
        std::string arg = argv[i];
        if (arg=="--dir" && i+2<argc) { mode="dir"; src=argv[++i]; dst=argv[++i]; }
        else if (arg=="--file" && i+2<argc) { mode="file"; src=argv[++i]; dst=argv[++i]; }
        else if (arg=="--ignore" && i+1<argc) { ignorePaths.emplace_back(argv[++i]); }
        else if (arg=="--delete") mirror=true;
        else if (arg=="--dry-run") dryRun=true;
        else if (arg=="--verbose") verbose=true;
        else if (arg=="--save-settings") saveSettingsFlag=true;
        else if (arg=="--save-log") saveLog=true;
        else if (arg=="--color") enableColors=true;
        else if (arg=="--sha256") useSha256=true;
        else if (arg=="-h" || arg=="--help") { printHelp(fs::path(argv[0]).filename().string()); return 0; }
#ifdef _WIN32
        else if (arg=="--add-to-path") { addToPath(fs::absolute(fs::path(argv[0])), true, enableColors); return 0; }
#endif
    }

    if (saveLog) logFile.open("sync.log", std::ios::app);
    if (mode.empty() && src.empty()) {
        auto loaded = loadSettings();
        if (!loaded.empty()) {
            logMsg("[*] INFO: using settings from " + SETTINGS_FILE, true, enableColors);
            mode = loaded["mode"]; src = loaded["src"]; dst = loaded["dst"];
            if (!mirror) mirror = (loaded["mirror"]=="true");
            if (!verbose) verbose = (loaded["verbose"]=="true");
            if (!useSha256) useSha256 = (loaded["sha256"]=="true");
        }
    }

    g_use_sha256 = useSha256;
    auto start = std::chrono::high_resolution_clock::now();

    if (mode=="dir") syncDir(src,dst,ignorePaths,dryRun,verbose,mirror,enableColors);
    else if (mode=="file") syncFile(src,dst,dryRun,verbose,enableColors);
    else { logMsg("[X] ERROR: No valid operation specified. Use --dir or --file.", true, enableColors); printHelp(fs::path(argv[0]).filename().string()); return 1; }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> dur = end - start;
    std::cout << "\n========================================\n";
    std::cout << "==> Sync completed in " << dur.count() << " seconds !!\n";
    std::cout << "========================================\n";

    if (saveSettingsFlag && !mode.empty()) {
        settings["mode"]=mode; settings["src"]=src.string(); settings["dst"]=dst.string();
        settings["mirror"]=mirror?"true":"false"; settings["verbose"]=verbose?"true":"false"; settings["sha256"]=g_use_sha256?"true":"false";
        saveSettings(settings);
        logMsg("[*] Settings saved to " + SETTINGS_FILE, true, enableColors);
    }
    return 0;
}
