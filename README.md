## 设计目标

为了学习FUSE，我打算实现一个简单的文件系统，名为`minfuse`。minfuse将元数据和数据存储在内存中。

为了易于学习，minfuse的开发有如下几个目标：
1. 能够进行fio测试
2. 尽量减少重复的异常处理代码

## Some important structures, functions and macros

先介绍一些重要的结构体、函数和宏定义。


1. `fuse_args`
This structure is used to handle command-line arguments passed to a FUSE filesystem.

```c
struct fuse_args {
	int argc; // number of arguments
	char **argv; // argument vector, NULL terminated
	int allocated; // is argv allocated?
};
```

2. `FUSE_ARGS_INIT`
Initializes a struct fuse_args with argc and argv, and `allocated` set to 0.

```c
#define FUSE_ARGS_INIT(argc, argv) { argc, argv, 0 }
```

3. `fuse_cmdline_opts`
A structure used to store command-line options parsed from the arguments. This structure helps manage and configure the FUSE filesystem based on user inputs.

```c
struct fuse_cmdline_opts {
	int singlethread;
	int foreground;
	int debug;
	int nodefault_subtype;
	char *mountpoint;
	int show_version;
	int show_help;
	int clone_fd;
	unsigned int max_idle_threads; 
	unsigned int max_threads; // This was added in libfuse 3.12
};
```

This structure can be populated using the `fuse_parse_cmdline` function.
```c
struct fuse_cmdline_opts opts;
fuse_parse_cmdline(&args,&opts);
```

4. `fuse_session_new`
Creates a new low-level session. This function accepts most file-system independent mount options.
```c
struct fuse_session *fuse_session_new(struct fuse_args *args,const struct fuse_lowlevel_ops *op,
size_t op_size, void *userdata);
```

5. `fuse_set_signal_handlers`
This function installs signal handlers for the signals `SIGHUP`, `SIGINT`, and `SIGTERM` that will attempt to unmount the file system. If there is already a signal handler installed for any of these signals then it is not replaced. This function returns zero on success and -1 on failure.

```c
int fuse_set_signal_handlers(struct fuse_session *se);
```

6. `fuse_lowlevel_ops`
This structure represents the low-level filesystem operations
```c
struct fuse_lowlevel_ops {
	// Called when libfuse establishes communication with the FUSE kernel module.
	void (*init) (void *userdata, struct fuse_conn_info *conn);

	// Cleans up filesystem, called on filesystem exit.
	void (*destroy) (void *userdata);

	// Look up a directory entry by name and get its attributes.
	void (*lookup) (fuse_req_t req, fuse_ino_t parent, const char *name);

	// Can be called to forget about an inode
	void (*forget) (fuse_req_t req, fuse_ino_t ino, uint64_t nlookup);

	// Called to get file attributes
	void (*getattr) (fuse_req_t req, fuse_ino_t ino,
			 struct fuse_file_info *fi);

	// Called to set file attributes
	void (*setattr) (fuse_req_t req, fuse_ino_t ino, struct stat *attr,
			 int to_set, struct fuse_file_info *fi);

	// Called to read the target of a symbolic link
	void (*readlink) (fuse_req_t req, fuse_ino_t ino);

	// Called to create a file node
	void (*mknod) (fuse_req_t req, fuse_ino_t parent, const char *name,
		       mode_t mode, dev_t rdev);

	// Called to create a directory
	void (*mkdir) (fuse_req_t req, fuse_ino_t parent, const char *name,
		       mode_t mode);

	// Called to remove a file
	void (*unlink) (fuse_req_t req, fuse_ino_t parent, const char *name);

	// Called to remove a directory
	void (*rmdir) (fuse_req_t req, fuse_ino_t parent, const char *name);

	// Called to create a symbolic link
	void (*symlink) (fuse_req_t req, const char *link, fuse_ino_t parent,
			 const char *name);

	// Called to rename a file or directory
	void (*rename) (fuse_req_t req, fuse_ino_t parent, const char *name,
			fuse_ino_t newparent, const char *newname,
			unsigned int flags);

	// Called to create a hard link
	void (*link) (fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
		      const char *newname);

	// Called to open a file
	void (*open) (fuse_req_t req, fuse_ino_t ino,
		      struct fuse_file_info *fi);

	// Called to read data from a file
	void (*read) (fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
		      struct fuse_file_info *fi);

	// Called to write data to a file
	void (*write) (fuse_req_t req, fuse_ino_t ino, const char *buf,
		       size_t size, off_t off, struct fuse_file_info *fi);

	// Called on each close() of the opened file, for flushing cached data
	void (*flush) (fuse_req_t req, fuse_ino_t ino,
		       struct fuse_file_info *fi);

	// Called to release an open file (when there are no more references to an open file i.e all file descriptors are closed and all memory mappings are unmapped)
	void (*release) (fuse_req_t req, fuse_ino_t ino,
			 struct fuse_file_info *fi);

	// Called to synchronize file contents
	void (*fsync) (fuse_req_t req, fuse_ino_t ino, int datasync,
		       struct fuse_file_info *fi);

	// Called to open a directory
	void (*opendir) (fuse_req_t req, fuse_ino_t ino,
			 struct fuse_file_info *fi);

	// Called to read directory entries
	void (*readdir) (fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
			 struct fuse_file_info *fi);

	// Called to release an open directory
	void (*releasedir) (fuse_req_t req, fuse_ino_t ino,
			    struct fuse_file_info *fi);

	// Called to synchronize directory contents
	void (*fsyncdir) (fuse_req_t req, fuse_ino_t ino, int datasync,
			  struct fuse_file_info *fi);

	// Called to get file system statistics
	void (*statfs) (fuse_req_t req, fuse_ino_t ino);

	// Called to set an extended attribute
	void (*setxattr) (fuse_req_t req, fuse_ino_t ino, const char *name,
			  const char *value, size_t size, int flags);

	// Called to get an extended attribute
	void (*getxattr) (fuse_req_t req, fuse_ino_t ino, const char *name,
			  size_t size);

	// Called to list extended attribute names
	void (*listxattr) (fuse_req_t req, fuse_ino_t ino, size_t size);

	// Called to remove an extended attribute
	void (*removexattr) (fuse_req_t req, fuse_ino_t ino, const char *name);

	// Called to check file-access permissions
	void (*access) (fuse_req_t req, fuse_ino_t ino, int mask);

	// Called to create and open a file
	void (*create) (fuse_req_t req, fuse_ino_t parent, const char *name,
			mode_t mode, struct fuse_file_info *fi);

	// Called to get a file lock
	void (*getlk) (fuse_req_t req, fuse_ino_t ino,
		       struct fuse_file_info *fi, struct flock *lock);

	// Called to set a file lock
	void (*setlk) (fuse_req_t req, fuse_ino_t ino,
		       struct fuse_file_info *fi,
		       struct flock *lock, int sleep);

	// Called to map a block index within file to a block index within device
	void (*bmap) (fuse_req_t req, fuse_ino_t ino, size_t blocksize,
		      uint64_t idx);

	// Called to poll a file for I/O readiness.
	void (*poll) (fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
		      struct fuse_pollhandle *ph);

	// Called to write a buffer to a file.
	void (*write_buf) (fuse_req_t req, fuse_ino_t ino,
			   struct fuse_bufvec *bufv, off_t off,
			   struct fuse_file_info *fi);

	// Called to reply to a retrieve operation.
	void (*retrieve_reply) (fuse_req_t req, void *cookie, fuse_ino_t ino,
				off_t offset, struct fuse_bufvec *bufv);

	// Called to forget multiple inodes
	void (*forget_multi) (fuse_req_t req, size_t count,
			      struct fuse_forget_data *forgets);

	// Called to acquire, modify or release a file lock
	void (*flock) (fuse_req_t req, fuse_ino_t ino,
		       struct fuse_file_info *fi, int op);

	//  Called to allocate space to a file
	void (*fallocate) (fuse_req_t req, fuse_ino_t ino, int mode,
		       off_t offset, off_t length, struct fuse_file_info *fi);

	// Called to read a directory entry with attributes 
	void (*readdirplus) (fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
			 struct fuse_file_info *fi);

	// To copy a range of data from one file to another
	void (*copy_file_range) (fuse_req_t req, fuse_ino_t ino_in,
				 off_t off_in, struct fuse_file_info *fi_in,
				 fuse_ino_t ino_out, off_t off_out,
				 struct fuse_file_info *fi_out, size_t len,
				 int flags);

	// The lseek operation, for specifying new file offsets past the current end of the file.
	void (*lseek) (fuse_req_t req, fuse_ino_t ino, off_t off, int whence,
		       struct fuse_file_info *fi);
};

```

7. `fuse_session_loop`
Enter a single threaded, blocking event loop. The loop can be terminated through signals if signal handlers have been pre-registered.

```c
int fuse_session_loop(struct fuse_session *se);
```

8. `fuse_session_unmount`
This function ensures that the file system is unmounted.
```c
void fuse_session_unmount(struct fuse_session *se);
```

9. `fuse_reply_*`
These types of functions (for example, fuse_reply_entry, fuse_reply_open, etc.) are used to send responses back to the FUSE kernel module from the user space filesystem implementation. Each `fuse_reply_*` type of function corresponds to a specific type of response that can be sent, depending on the operation being performed.

## reference

1. [Creating a custom filesytem using FUSE - Part 1](https://sh4dy.com/2024/06/24/fuse_01/)