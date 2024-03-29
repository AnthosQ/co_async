#pragma once

#include <coroutine>
#include <chrono>
#include <cstdint>
#include <utility>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <filesystem>
#include <co_async/task.hpp>
#include <co_async/platform.hpp>
#include <co_async/system_loop.hpp>

#if CO_ASYNC_PLAT_LINUX
#include <co_async/error_handling.hpp>
#include <unistd.h>
#include <fcntl.h>

namespace co_async {

struct [[nodiscard]] FileHandle {
    FileHandle() : mFileNo(-1) {}

    explicit FileHandle(int fileNo) noexcept : mFileNo(fileNo) {}

    FileHandle(FileHandle &&that) noexcept : mFileNo(that.mFileNo) {
        that.mFileNo = -1;
    }

    FileHandle &operator=(FileHandle &&that) noexcept {
        std::swap(mFileNo, that.mFileNo);
        return *this;
    }

    ~FileHandle() {
        if (mFileNo != -1)
            close(mFileNo);
    }

    int fileNo() const noexcept {
        return mFileNo;
    }

    int releaseFile() noexcept {
        int ret = mFileNo;
        mFileNo = -1;
        return ret;
    }

private:
    int mFileNo;
};

struct [[nodiscard]] FilePath {
    explicit FilePath(std::filesystem::path path, int dirfd = AT_FDCWD)
        : mPath(path),
          mDirFd(dirfd) {}

    char const *c_str() const noexcept {
        return mPath.c_str();
    }

    std::filesystem::path path() const {
        return mPath;
    }

    int dir_file() const noexcept {
        return mDirFd;
    }

private:
    std::filesystem::path mPath;
    int mDirFd;
};

struct FileStat {
    struct statx *getNativeStatx() {
        return &mStatx;
    }

private:
    struct statx mStatx;
};

enum class OpenMode : int {
    Read = O_RDONLY,
    Write = O_WRONLY | O_TRUNC | O_CREAT,
    ReadWrite = O_RDWR | O_CREAT,
    Append = O_WRONLY | O_APPEND | O_CREAT,
};

inline Task<FileHandle> fs_open(FileHandle const &dir, FilePath path,
                                OpenMode mode, mode_t access = 0644) {
    int oflags = (int)mode;
    int fd = co_await uring_openat(loop, path.dir_file(), path.c_str(), oflags,
                                   access);
    FileHandle file(fd);
    co_return file;
}

inline Task<> fs_close(FileHandle &&file) {
    co_await uring_close(loop, file.fileNo());
    file.releaseFile();
}

inline Task<> fs_mkdir(FilePath path, mode_t access = 0755) {
    co_await uring_mkdirat(loop, path.dir_file(), path.c_str(), access);
}

inline Task<> fs_link(FilePath oldpath, FilePath newpath) {
    co_await uring_linkat(loop, oldpath.dir_file(), oldpath.c_str(),
                          newpath.dir_file(), newpath.c_str(), 0);
}

inline Task<> fs_symlink(FilePath target, FilePath linkpath) {
    co_await uring_symlinkat(loop, target.c_str(), linkpath.dir_file(),
                             linkpath.c_str());
}

inline Task<> fs_unlink(FilePath path) {
    co_await uring_unlinkat(loop, path.dir_file(), path.c_str(), 0);
}

inline Task<> fs_rmdir(FilePath path) {
    co_await uring_unlinkat(loop, path.dir_file(), path.c_str(), AT_REMOVEDIR);
}

inline Task<std::optional<FileStat>> fs_stat(FilePath path) {
    FileStat ret;
    int res = co_await uring_statx(loop, path.dir_file(), path.c_str(), 0,
                                   STATX_ALL, ret.getNativeStatx());
    int const allowed[] = {ENOENT, ENOTDIR, ENAMETOOLONG, ELOOP, EACCES};
    if (res < 0) {
        res = -res;
        for (auto e: allowed) {
            if (res == e) {
                co_return std::nullopt;
            }
        }
    }
    co_return ret;
}

inline Task<std::size_t> fs_read(FileHandle &file, std::span<char> buffer,
                                 std::uint64_t offset = -1) {
    return uring_read(loop, file.fileNo(), buffer, offset);
}

inline Task<std::size_t> fs_write(FileHandle &file,
                                  std::span<char const> buffer,
                                  std::uint64_t offset = -1) {
    return uring_write(loop, file.fileNo(), buffer, offset);
}

} // namespace co_async
#endif

#if CO_ASYNC_PLAT_WIN32
#error "Not Implemented"
#endif

#if CO_ASYNC_PLAT_APPLE
#error "Not Implemented"
#endif

#if CO_ASYNC_PLAT_UNKNOWN
#error "Not Implemented"
#endif
