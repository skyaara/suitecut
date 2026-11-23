// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace suitecut {
inline void ContextVideoActive(const std::atomic<bool>& canceled, double deadline,
                               const std::function<double()>& now) {
  if (canceled.load()) throw std::runtime_error("Native video file operation canceled");
  if (deadline && now() >= deadline)
    throw std::runtime_error("Native video original file operation deadline expired");
}
// Owns one descriptor for the duration of a single file operation. Sealed
// artifacts retain only identity metadata, never a descriptor, so many retained
// videos cannot exhaust the process descriptor table.
class ContextVideoFD {
 public:
  explicit ContextVideoFD(int fd = -1) : fd_(fd) {}
  ContextVideoFD(ContextVideoFD&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  ContextVideoFD& operator=(ContextVideoFD&& other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) close(fd_);
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  ContextVideoFD(const ContextVideoFD&) = delete;
  ContextVideoFD& operator=(const ContextVideoFD&) = delete;
  ~ContextVideoFD() {
    if (fd_ >= 0) close(fd_);
  }
  int get() const { return fd_; }

 private:
  int fd_;
};
class ContextVideoDirectory {
 public:
  static std::shared_ptr<ContextVideoDirectory> Create(const std::string& directory,
                                                       const std::atomic<bool>& canceled,
                                                       double deadline,
                                                       const std::function<double()>& now) {
    ContextVideoActive(canceled, deadline, now);
    if (directory.empty() || directory.find('\0') != std::string::npos ||
        !std::filesystem::path(directory).is_absolute())
      throw std::runtime_error("Native recordVideo.dir must be an absolute path");
    std::filesystem::create_directories(directory);
    const auto parent = std::filesystem::canonical(directory);
    std::string pattern = (parent / ".suitecut-video-XXXXXX").string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    ContextVideoActive(canceled, deadline, now);
    const auto created = mkdtemp(bytes.data());
    if (!created) throw std::runtime_error("Cannot create private native video directory");
    struct stat held{};
    {
      const ContextVideoFD fd(open(created, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
      if (fd.get() < 0 || fstat(fd.get(), &held)) {
        rmdir(created);
        throw std::runtime_error("Cannot retain native video directory");
      }
    }
    auto result = std::shared_ptr<ContextVideoDirectory>(new ContextVideoDirectory(created, held));
    ContextVideoActive(canceled, deadline, now);
    return result;
  }
  ~ContextVideoDirectory() {
    struct stat current{};
    if (!lstat(path_.c_str(), &current) && S_ISDIR(current.st_mode) && current.st_dev == dev_ &&
        current.st_ino == ino_)
      rmdir(path_.c_str());  // Empty-only, identity-checked cleanup.
  }
  const std::string& Path() const { return path_; }
  // Reopens the directory and proves it is still the created inode.
  ContextVideoFD Open() const {
    ContextVideoFD fd(open(path_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    struct stat current{};
    if (fd.get() < 0 || fstat(fd.get(), &current) || !S_ISDIR(current.st_mode) ||
        current.st_dev != dev_ || current.st_ino != ino_)
      throw std::runtime_error("Native video directory identity changed");
    return fd;
  }
  void Check() const { (void)Open(); }

 private:
  ContextVideoDirectory(std::string path, const struct stat& held)
      : path_(std::move(path)), dev_(held.st_dev), ino_(held.st_ino) {}
  std::string path_;
  dev_t dev_;
  ino_t ino_;
};
class ContextVideoFile {
 public:
  static std::shared_ptr<ContextVideoFile> Seal(std::shared_ptr<ContextVideoDirectory> directory,
                                                const std::string& name, uint64_t expected) {
    const auto dir = directory->Open();
    if (name.empty() || name.find('/') != std::string::npos || name == "." || name == ".." ||
        !expected)
      throw std::runtime_error("Invalid completed native video file metadata");
    struct stat linked{};
    if (fstatat(dir.get(), name.c_str(), &linked, AT_SYMLINK_NOFOLLOW) || !S_ISREG(linked.st_mode))
      throw std::runtime_error("Completed native video file is unavailable");
    const ContextVideoFD fd(
        openat(dir.get(), name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (fd.get() < 0) throw std::runtime_error("Completed native video file is unavailable");
    struct stat metadata{};
    if (fstat(fd.get(), &metadata) || !S_ISREG(metadata.st_mode) ||
        metadata.st_dev != linked.st_dev || metadata.st_ino != linked.st_ino ||
        metadata.st_size < 0 || static_cast<uint64_t>(metadata.st_size) != expected)
      throw std::runtime_error("Completed native video file does not match actual encoder bytes");
    return std::shared_ptr<ContextVideoFile>(
        new ContextVideoFile(std::move(directory), name, metadata));
  }
  std::string Path() const {
    (void)OpenIn(directory_->Open());
    return (std::filesystem::path(directory_->Path()) / name_).string();
  }
  void Copy(const std::string& destination, const std::atomic<bool>& canceled, double deadline,
            const std::function<double()>& now) const {
    ContextVideoActive(canceled, deadline, now);
    const auto source = OpenIn(directory_->Open());
    if (destination.empty() || destination.find('\0') != std::string::npos ||
        !std::filesystem::path(destination).is_absolute() ||
        std::filesystem::path(destination).filename().empty() ||
        std::filesystem::path(destination).filename() == "." ||
        std::filesystem::path(destination).filename() == "..")
      throw std::runtime_error("Native video saveAs requires an absolute file path");
    const auto target = std::filesystem::path(destination);
    std::filesystem::create_directories(target.parent_path());
    const auto parent = std::filesystem::canonical(target.parent_path());
    const int parent_fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent_fd < 0)
      throw std::runtime_error("Cannot retain native video saveAs destination directory");
    int output = -1;
    std::string temporary;
    bool committed = false;
    try {
      struct stat existing{};
      if (fstatat(parent_fd, target.filename().c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0 &&
          existing.st_dev == metadata_.st_dev && existing.st_ino == metadata_.st_ino) {
        ContextVideoActive(canceled, deadline, now);
        close(parent_fd);
        return;
      }
      static std::atomic<uint64_t> sequence{0};
      while (output < 0) {
        temporary =
            ".suitecut-video-save-" + std::to_string(getpid()) + "-" + std::to_string(++sequence);
        output = openat(parent_fd, temporary.c_str(),
                        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (output < 0 && errno != EEXIST) break;
      }
      if (output < 0) throw std::runtime_error("Cannot create native video saveAs staging file");
      std::vector<char> buffer(65536);
      uint64_t position = 0;
      while (position < static_cast<uint64_t>(metadata_.st_size)) {
        ContextVideoActive(canceled, deadline, now);
        const auto count = pread(
            source.get(), buffer.data(),
            std::min<uint64_t>(buffer.size(), static_cast<uint64_t>(metadata_.st_size) - position),
            static_cast<off_t>(position));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("Cannot read complete native video source");
        ssize_t offset = 0;
        while (offset < count) {
          ContextVideoActive(canceled, deadline, now);
          const auto wrote =
              write(output, buffer.data() + offset, static_cast<size_t>(count - offset));
          if (wrote < 0 && errno == EINTR) continue;
          if (wrote <= 0) throw std::runtime_error("Cannot write native video saveAs staging file");
          offset += wrote;
        }
        position += static_cast<uint64_t>(count);
      }
      CheckHeld(source.get());
      if (fsync(output)) throw std::runtime_error("Cannot flush native video saveAs file");
      ContextVideoActive(canceled, deadline, now);
      if (renameat(parent_fd, temporary.c_str(), parent_fd, target.filename().c_str()))
        throw std::runtime_error("Cannot publish native video saveAs file");
      committed = true;
      if (fsync(parent_fd))
        throw std::runtime_error("Native video saveAs committed but directory flush failed");
      ContextVideoActive(canceled, deadline, now);
      close(output);
      close(parent_fd);
    } catch (...) {
      if (output >= 0) close(output);
      if (!committed && !temporary.empty()) unlinkat(parent_fd, temporary.c_str(), 0);
      close(parent_fd);
      throw;
    }
  }
  void Delete(const std::atomic<bool>& canceled, double deadline,
              const std::function<double()>& now, bool& removed) const {
    ContextVideoActive(canceled, deadline, now);
    const auto dir = directory_->Open();
    (void)OpenIn(dir);
    if (unlinkat(dir.get(), name_.c_str(), 0))
      throw std::runtime_error("Cannot delete native video artifact");
    removed = true;  // Actual unlink is retained even if later durability/cancel checks fail.
    if (fsync(dir.get()))
      throw std::runtime_error("Native video artifact deleted but directory flush failed");
  }

 private:
  ContextVideoFile(std::shared_ptr<ContextVideoDirectory> directory, std::string name,
                   const struct stat& metadata)
      : directory_(std::move(directory)), name_(std::move(name)), metadata_(metadata) {}
  static bool SameModified(const struct stat& a, const struct stat& b) {
#if defined(__APPLE__)
    return a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec &&
           a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec;
#else
    return a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec;
#endif
  }
  void CheckHeld(int fd) const {
    struct stat actual{};
    if (fstat(fd, &actual) || !S_ISREG(actual.st_mode) || actual.st_dev != metadata_.st_dev ||
        actual.st_ino != metadata_.st_ino || actual.st_size != metadata_.st_size ||
        !SameModified(actual, metadata_))
      throw std::runtime_error("Native video artifact bytes changed after encoder finalization");
  }
  // Reopens the sealed artifact by name inside a verified directory descriptor;
  // a replacement pathname is rejected before any byte is read from it.
  ContextVideoFD OpenIn(const ContextVideoFD& dir) const {
    struct stat linked{};
    if (fstatat(dir.get(), name_.c_str(), &linked, AT_SYMLINK_NOFOLLOW) ||
        !S_ISREG(linked.st_mode) || linked.st_dev != metadata_.st_dev ||
        linked.st_ino != metadata_.st_ino)
      throw std::runtime_error("Native video artifact path no longer identifies its owned file");
    ContextVideoFD fd(
        openat(dir.get(), name_.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (fd.get() < 0)
      throw std::runtime_error("Native video artifact path no longer identifies its owned file");
    CheckHeld(fd.get());
    return fd;
  }
  std::shared_ptr<ContextVideoDirectory> directory_;
  std::string name_;
  struct stat metadata_;
};
}  // namespace suitecut
