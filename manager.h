#pragma once

#include <sys/types.h>

#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "spdlog/spdlog.h"

// Max 10 files can be stored in the root directory
#define MAX_FILES 10

#define MAX_FILENAME_LEN 64

struct DataBlock {
  char *data;
  mutable std::shared_mutex mutex;
};

// Structure for storing information about a file in our filesystem
struct file_info {
  size_t size;                           // size of the file
  std::unordered_map<int, DataBlock> data;  // file contents
  char *name;                            // file name
  mode_t mode;                           // mode (permissions)
  ino_t ino;                             // inode number
  bool is_used;                          // is the current slot used
};

// parent inode + file_name
typedef std::pair<ino_t, std::string> FileNameKey;

struct FileNameKeyHash {
  std::size_t operator()(const FileNameKey &item) const {
    std::size_t h1 = std::hash<int32_t>()(item.first);
    std::size_t h2 = std::hash<std::string>()(item.second);
    return (h1 << 1) ^ h2;
  }
};

class FdManager {
 public:
  static FdManager &GetInstance() {
    static FdManager instance;
    return instance;
  }

  FdManager(const FdManager &) = delete;
  FdManager &operator=(const FdManager &) = delete;

  int GetFd() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (free_fds_.empty()) [[unlikely]] {
      SPDLOG_ERROR("fd exhausted");
      return -1;
    }
    int fd = free_fds_.front();
    free_fds_.pop_front();
    return fd;
  }

  void ReleaseFd(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    free_fds_.push_back(fd);
  }

 private:
  FdManager() {
    for (int i = 0; i < kMaxFd; i++) {
      free_fds_.push_back(i);
    }
  }
  ~FdManager() = default;

  std::mutex mutex_;
  std::list<int> free_fds_;
  const int kMaxFd = 65535;
};

class OpenFile : public std::enable_shared_from_this<OpenFile> {
 public:
  OpenFile(int fd, file_info *info) : fd_(fd), info_(info) {}
  ~OpenFile() = default;

  void read(fuse_req_t req, size_t size, off_t off) {
    int block_idx = off / kMemBlockSize;
    int block_off = off % kMemBlockSize;
    if (info_->data.find(block_idx) == info_->data.end()) {
      fuse_reply_buf(req, nullptr, size);
      return;
    }
    std::shared_lock block_lock(info_->data[block_idx].mutex);
    // TODO 垮block读取
    reply_buf_limited(req, info_->data[block_idx].data, kMemBlockSize, block_off,
                      size);
  }

  void write(fuse_req_t req, const char *buf, size_t size, off_t off) {
    int block_idx = off / kMemBlockSize;
    int block_off = off % kMemBlockSize;
    if (info_->data.find(block_idx) == info_->data.end()) {
      info_->data[block_idx].data = (char *)calloc(kMemBlockSize, sizeof(char));
    }
    if (info_->data[block_idx].data == nullptr) [[unlikely]] {
      SPDLOG_ERROR("calloc failed");
      fuse_reply_err(req, ENOMEM);
      return;
    }
    info_->size = std::max(info_->size, off + size);
    std::unique_lock block_lock(info_->data[block_idx].mutex);
    memcpy(info_->data[block_idx].data + block_off, buf, size);
    fuse_reply_write(req, size);
  }

 private:
  int fd_;
  file_info *info_;
  std::shared_mutex mutex_;
};
typedef std::shared_ptr<OpenFile> OpenFilePtr;

class FileManager {
 public:
  static FileManager &GetInstance() {
    static FileManager instance;
    return instance;
  }

  FileManager(const FileManager &) = delete;
  FileManager &operator=(const FileManager &) = delete;

  void InitFiles() {
    for (int i = 0; i < MAX_FILES; i++) {
      files_[i].mode = 0;
      files_[i].size = 0;
      files_[i].is_used = false;
      files_[i].name = (char *)malloc(MAX_FILENAME_LEN);
      files_[i].ino = 2;
    }
  }

  file_info *GetFileByName(const char *name) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (int i = 0; i < MAX_FILES; i++) {
      if (files_[i].is_used && strcmp(files_[i].name, name) == 0) {
        return &files_[i];
      }
    }
    return nullptr;
  }

  file_info *GetFileByIno(ino_t ino) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (int i = 0; i < MAX_FILES; i++) {
      if (files_[i].is_used && files_[i].ino == ino) {
        return &files_[i];
      }
    }
    return nullptr;
  }

  int CreateFile(const char *name, mode_t mode, struct fuse_file_info *fi,
                 ino_t *ino) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (int i = 0; i < MAX_FILES; i++) {
      if (!files_[i].is_used) {
        files_[i].is_used = true;
        files_[i].mode = S_IFREG | mode;
        files_[i].size = 0;
        files_[i].ino = i + 2;
        strncpy(files_[i].name, name, strlen(name));
        files_[i].name[strlen(name)] = 0x0;
        *ino = files_[i].ino;

        int fd = FdManager::GetInstance().GetFd();
        if (fd == -1) {
          return EMFILE;
        }
        AddOpenFile(fd, &files_[i]);
        fi->fh = fd;
        return 0;
      }
    }
    return ENOSPC;
  }

  int UnlinkFile(const char *name) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (int i = 0; i < MAX_FILES; i++) {
      if (files_[i].is_used && strcmp(files_[i].name, name) == 0) {
        files_[i].is_used = false;
        files_[i].size = 0;
        for (const auto &[_, block] : files_[i].data) {
          free(block.data);
        }
        files_[i].ino = 0;
        return 0;
      }
    }
    return ENOENT;
  }

  std::vector<std::pair<const char *, ino_t>> ListFiles() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    std::vector<std::pair<const char *, ino_t>> files;
    for (int i = 0; i < MAX_FILES; i++) {
      if (files_[i].is_used) {
        files.push_back(std::make_pair(files_[i].name, files_[i].ino));
      }
    }
    return files;
  }

  // 获取指定 fd 对应的 OpenFilePtr
  OpenFilePtr GetOpenFile(int fd) {
    std::lock_guard<std::mutex> lock(open_file_mutex_);
    auto it = open_files_.find(fd);
    if (it == open_files_.end()) {
      return nullptr;
    }
    return it->second;
  }

  // 将 fd 与 OpenFilePtr 建立关联
  void AddOpenFile(int fd, file_info *info) {
    OpenFilePtr open_file = std::make_shared<OpenFile>(fd, info);
    std::lock_guard<std::mutex> lock(open_file_mutex_);
    open_files_[fd] = open_file;
  }

  // 释放一个 fd
  void CloseFile(int fd) {
    std::lock_guard<std::mutex> lock(open_file_mutex_);
    open_files_.erase(fd);
  }

 private:
  FileManager() = default;
  ~FileManager() = default;

  std::mutex file_mutex_;
  struct file_info files_[MAX_FILES];
  std::mutex open_file_mutex_;
  std::unordered_map<int, OpenFilePtr> open_files_;
};
