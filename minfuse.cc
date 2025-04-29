// Do not forget to add the macro FUSE_USE_VERSION
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 17)

#include <dirent.h>
#include <errno.h>
// #include <mimalloc-override.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "manager.h"
#include "spdlog/cfg/env.h"  // support for loading levels from the environment variable
#include "spdlog/fmt/ostr.h"  // support for user defined types
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/spdlog.h"

static auto &fm = FileManager::GetInstance();

// Structure for handling directory entries
struct dirbuf {
  char *p;
  size_t size;
};

// A macro for adding a new entry
#define DIRBUF_ADDENTRY(req, b, name, ino)                              \
  do {                                                                  \
    struct stat stbuf;                                                  \
    size_t oldsize = (b)->size;                                         \
    (b)->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);        \
    (b)->p = (char *)realloc((b)->p, (b)->size);                        \
    memset(&stbuf, 0, sizeof(stbuf));                                   \
    stbuf.st_ino = ino;                                                 \
    fuse_add_direntry(req, (b)->p + oldsize, (b)->size - oldsize, name, \
                      &stbuf, (b)->size);                               \
  } while (0)

static void init_handler(void *userdata, struct fuse_conn_info *conn) {
  // Called when libfuse establishes communication with the FUSE kernel module.
  SPDLOG_INFO("init_handler called");
  SPDLOG_DEBUG("FUSE max_write: {}", conn->max_write);
  if (conn->capable & FUSE_CAP_PASSTHROUGH) {
    SPDLOG_INFO("FUSE_CAP_PASSTHROUGH is supported");
  }
  fm.InitFiles();
}

static void lookup_handler(fuse_req_t req, fuse_ino_t parent,
                           const char *name) {
  SPDLOG_DEBUG("lookup_handler called: looking for {}", name);
  if (nullptr == name) [[unlikely]] {
    fuse_reply_err(req, EINVAL);
    return;
  }
  if (strlen(name) > NAME_MAX) [[unlikely]] {
    fuse_reply_err(req, ENAMETOOLONG);
    return;
  }

  struct fuse_entry_param e;
  memset(&e, 0, sizeof(e));

  if (parent == kRootInode) {
    file_info *file = fm.GetFileByName(name);
    if (file != nullptr) {
      e.ino = file->ino;
      e.attr.st_ino = file->ino;
      e.attr.st_mode = file->mode;
      e.attr_timeout = 1.0;
      e.entry_timeout = 1.0;
      e.attr.st_nlink = 1;
      e.attr.st_size = file->size;
      fuse_reply_entry(req, &e);
      return;
    }
  }
  // No entry found
  fuse_reply_err(req, ENOENT);
}

static void getattr_handler(fuse_req_t req, fuse_ino_t ino,
                            struct fuse_file_info *fi) {
  SPDLOG_DEBUG("getattr_handler called, inode: {}", ino);
  struct stat stbuf;

  // Is a directory (root directory of our filesystem)
  if (ino == kRootInode) {
    stbuf.st_mode = S_IFDIR | 0755;
    stbuf.st_nlink = 2;
    fuse_reply_attr(req, &stbuf, 1.0);
    return;
  } else {
    file_info *file = fm.GetFileByIno(ino);
    if (file != nullptr) {
      stbuf.st_ino = ino;
      stbuf.st_mode = file->mode;
      stbuf.st_nlink = 1;
      stbuf.st_size = file->size;
      fuse_reply_attr(req, &stbuf, 1.0);
      return;
    }
  }

  fuse_reply_err(req, ENOENT);
}

static void setattr_handler(fuse_req_t req, fuse_ino_t ino, struct stat *attr,
                            int to_set, struct fuse_file_info *fi) {
  SPDLOG_DEBUG("setattr_handler called");
  struct stat stbuf;

  if (ino < 2) {
    fuse_reply_err(req, EISDIR);
    return;
  }
  file_info *file = fm.GetFileByIno(ino);
  if (file != nullptr) {
    stbuf.st_ino = ino;
    stbuf.st_mode = file->mode;
    stbuf.st_nlink = 1;
    stbuf.st_size = file->size;

    if (to_set & FUSE_SET_ATTR_ATIME) {
      stbuf.st_atime = attr->st_atime;
    }
    if (to_set & FUSE_SET_ATTR_MTIME) {
      stbuf.st_mtime = attr->st_mtime;
    }
    if (to_set & FUSE_SET_ATTR_CTIME) {
      stbuf.st_ctime = attr->st_ctime;
    }
    fuse_reply_attr(req, &stbuf, 1.0);
    return;
  }
}

static void unlink_handler(fuse_req_t req, fuse_ino_t parent,
                           const char *name) {
  SPDLOG_DEBUG("unlink_handler called, filename: {}", name);
  if (parent != kRootInode) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  int err = fm.UnlinkFile(name);
  fuse_reply_err(req, err);
}

void readdir_handler(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                     struct fuse_file_info *fi) {
  SPDLOG_DEBUG("readdir_handler called, inode: {}", ino);
  (void)fi;

  // Currently there's only one directory present in our filesystem, the root directory
  if (ino != kRootInode)
    fuse_reply_err(req, ENOTDIR);
  else {
    struct dirbuf b;
    memset(&b, 0, sizeof(b));
    // Add entries for . and ..
    DIRBUF_ADDENTRY(req, &b, ".", 1);
    DIRBUF_ADDENTRY(req, &b, "..", 1);

    std::vector<std::pair<const char *, ino_t>> files =
        fm.ListFiles();
    for (auto &file : files) {
      SPDLOG_DEBUG("Adding entry for filename: {}, inode: {}", file.first,
                   file.second);
      DIRBUF_ADDENTRY(req, &b, file.first, file.second);
    }

    reply_buf_limited(req, b.p, b.size, off, size);
    free(b.p);
  }
}

void opendir_handler(fuse_req_t req, fuse_ino_t ino,
                     struct fuse_file_info *fi) {
  SPDLOG_DEBUG("opendir_handler called");
  if (ino != 1) {
    // Inode number for the only directory right now is 1
    fuse_reply_err(req, ENOTDIR);
    return;
  }
  fuse_reply_open(req, fi);
}

static void open_handler(fuse_req_t req, fuse_ino_t ino,
                         struct fuse_file_info *fi) {
  SPDLOG_DEBUG("open_handler called, inode: {}", ino);
  if (ino < 2) {
    // Inode number 1, i.e a directory
    fuse_reply_err(req, EISDIR);
    return;
  }
  // Open the file
  file_info *file = fm.GetFileByIno(ino);
  if (file == nullptr) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  int fd = FdManager::GetInstance().GetFd();
  if (fd == -1) {
    fuse_reply_err(req, EMFILE);
    return;
  }
  fm.AddOpenFile(fd, file);
  fi->fh = fd;
  fuse_reply_open(req, fi);
}

static void read_handler(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                         struct fuse_file_info *fi) {
  SPDLOG_DEBUG("inode: {}, fd: {}, offset: {}, size: {}", ino, fi->fh, off,
               size);
  if (ino < 2) {
    fuse_reply_err(req, EISDIR);
  } else {
    OpenFilePtr open_file = fm.GetOpenFile(fi->fh);
    if (!open_file) {
      SPDLOG_ERROR("file not found, inode: {}, fd: {}", ino, fi->fh);
      fuse_reply_err(req, EBADF);
      return;
    }
    open_file->read(req, size, off);
  }
}

static void write_handler(fuse_req_t req, fuse_ino_t ino, const char *buf,
                          size_t size, off_t off, struct fuse_file_info *fi) {
  SPDLOG_DEBUG("inode: {}, fd: {}, offset: {}, size: {}", ino, fi->fh, off,
               size);
  if (ino < 2) {
    fuse_reply_err(req, EISDIR);
    return;
  }
  OpenFilePtr open_file = fm.GetOpenFile(fi->fh);
  if (!open_file) {
    SPDLOG_ERROR("file not found, inode: {}, fd: {}", ino, fi->fh);
    fuse_reply_err(req, EBADF);
    return;
  }
  open_file->write(req, buf, size, off);
}

static void release_handler(fuse_req_t req, fuse_ino_t ino,
                            struct fuse_file_info *fi) {
  SPDLOG_DEBUG("release_handler called, inode: {}, fd: {}", ino, fi->fh);
  OpenFilePtr info = fm.GetOpenFile(fi->fh);
  if (!info) {
    SPDLOG_ERROR("file not found, inode: {}, fd: {}", ino, fi->fh);
    fuse_reply_err(req, EBADF);
    return;
  }
  fm.CloseFile(fi->fh);
  fuse_reply_err(req, 0);
}

static void statfs_handler(fuse_req_t req, fuse_ino_t ino) {
  SPDLOG_DEBUG("statfs_handler called, inode: {}", ino);
  struct statvfs stbuf;
  memset(&stbuf, 0, sizeof(stbuf));
  // f_bsize: 文件系统块大小
  stbuf.f_bsize = 512;
  // f_frsize: 单位字节, df命令中的块大小
  stbuf.f_frsize = 512;
  // f_blocks: 文件系统数据块总数
  stbuf.f_blocks = 4096;
  // f_bfree: 可用块数
  stbuf.f_bfree = 2048;
  stbuf.f_bavail = stbuf.f_bfree;
  // f_files: 文件结点总数
  stbuf.f_files = MAX_FILES;
  // f_ffree: 可用文件结点数
  stbuf.f_ffree = MAX_FILES;
  stbuf.f_favail = stbuf.f_ffree;
  // f_fsid: 文件系统标识 ID
  stbuf.f_fsid = 0;
  // f_namemax: 最大文件长度
  stbuf.f_namemax = NAME_MAX;

  fuse_reply_statfs(req, &stbuf);
}

static void create_handler(fuse_req_t req, fuse_ino_t parent, const char *name,
                           mode_t mode, struct fuse_file_info *fi) {
  struct fuse_entry_param e;
  memset(&e, 0, sizeof(e));

  SPDLOG_DEBUG("create_handler called, filename: {}, mode: {}", name, mode);

  if (parent != kRootInode) {
    // The root directory is the parent of all files
    fuse_reply_err(req, ENOENT);
    return;
  }

  ino_t ino;
  int err = fm.CreateFile(name, mode, fi, &ino);
  if (err != 0) {
    fuse_reply_err(req, err);
    return;
  }
  e.ino = ino;
  e.attr.st_ino = ino;
  e.attr.st_mode = S_IFREG | mode;
  e.attr.st_nlink = 1;
  e.attr.st_uid = getuid();
  e.attr.st_gid = getgid();
  e.attr.st_size = 0x0;
  e.attr.st_blksize = 512;
  e.attr.st_blocks = 0;

  fuse_reply_create(req, &e, fi);
}

static struct fuse_lowlevel_ops operations = {
    .init = init_handler,
    .destroy = nullptr,
    .lookup = lookup_handler,
    .forget = nullptr,
    .getattr = getattr_handler,
    .setattr = setattr_handler,
    .unlink = unlink_handler,
    .open = open_handler,
    .read = read_handler,
    .write = write_handler,
    .release = release_handler,
    .opendir = opendir_handler,
    .readdir = readdir_handler,
    .statfs = statfs_handler,
    .create = create_handler,
};

int main(int argc, char **argv) {
  // Create a file rotating logger with 100 MB size max and 3 rotated files
  auto max_size = 1024 * 1024 * 100;
  auto max_files = 3;
  auto logger = spdlog::rotating_logger_mt("mfs_spdlog", "/var/log/mfs/mfs.log",
                                           max_size, max_files);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::debug);
  // https://github.com/gabime/spdlog/wiki/3.-Custom-formatting
  spdlog::set_pattern("%Y%m%d %H:%M:%S.%e %t %l %s:%# %! - %v");
  spdlog::flush_on(spdlog::level::info);
  spdlog::flush_every(std::chrono::seconds(1));
  SPDLOG_INFO("Welcome to spdlog version {}.{}.{}!", SPDLOG_VER_MAJOR,
              SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);

  int retval = 0;

  std::vector<char *> argval;
  for (int i = 0; i < argc; i++) {
    argval.push_back(argv[i]);
  }
  argval.push_back((char *)"-f");
  argval.push_back((char *)"-oallow_other");
  argval.push_back((char *)"-orw");
  for (size_t i = 0; i < argval.size(); ++i) {
    SPDLOG_INFO("fuse args: {}", argval[i]);
  }
  int argcnt = argval.size();
  char **argv_ptr = argval.data();
  struct fuse_args args = FUSE_ARGS_INIT(argcnt, argv_ptr);

  struct fuse_cmdline_opts opts;
  struct fuse_session *se;

  if (fuse_parse_cmdline(&args, &opts)) {
    return 1;
  }
  if (opts.show_help) {
    printf("Usage: %s [options] <mountpoint>\n", argv[0]);
    fuse_cmdline_help();
    return 0;
  }
  if (opts.show_version) {
    fuse_lowlevel_version();
    return 0;
  }
  if (opts.mountpoint == nullptr) {
    printf("Usage: %s [options] <mountpoint>\n", argv[0]);
    return 1;
  }

  se = fuse_session_new(&args, &operations, sizeof(operations), nullptr);
  if (se == nullptr) {
    free(opts.mountpoint);
    fuse_opt_free_args(&args);
    return 1;
  }

  if (fuse_set_signal_handlers(se) != 0) {
    retval = 1;
    goto errlabel_two;
  }

  if (fuse_session_mount(se, opts.mountpoint) != 0) {
    retval = 1;
    goto errlabel_one;
  }

  if (kThreadCnt > 1) {
    struct fuse_loop_config *config = fuse_loop_cfg_create();
    fuse_loop_cfg_set_idle_threads(config, -1);
    fuse_loop_cfg_set_max_threads(config, kThreadCnt);
    fuse_loop_cfg_set_clone_fd(config, 1);
    fuse_session_loop_mt(se, config);
    fuse_session_unmount(se);
    fuse_loop_cfg_destroy(config);
  } else {
    fuse_session_loop(se);
    fuse_session_unmount(se);
  }

errlabel_one:
  fuse_remove_signal_handlers(se);

errlabel_two:
  fuse_session_destroy(se);
  free(opts.mountpoint);
  fuse_opt_free_args(&args);
  return retval;
}