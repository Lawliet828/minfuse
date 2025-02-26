#pragma once

#include <fuse3/fuse_lowlevel.h>

#include <algorithm>

inline const fuse_ino_t kRootInode = 1;
inline const int kThreadCnt = 4;
inline const int kMemBlockSize = 64 * 1024 * 1024;

inline int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
                             off_t off, size_t maxsize) {
  if (off < bufsize)
    return fuse_reply_buf(req, buf + off, std::min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, nullptr, 0);
}