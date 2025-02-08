fio -direct=1 -iodepth=32 -rw=randrw -ioengine=libaio -bs=4k -size=1G -numjobs=1 -runtime=30 -group_reporting -filename=/tmp/mfs/testfile -fallocate=none -name=RandRW -verify=md5 --do_verify=1 --verify_fatal=1

2025/01/07

ptmalloc, MAX_FILES为10, files使用数组
   READ: bw=16.3MiB/s (17.1MB/s), 16.3MiB/s-16.3MiB/s (17.1MB/s-17.1MB/s), io=78.6MiB (82.4MB), run=4831-4831msec
  WRITE: bw=70.7MiB/s (74.1MB/s), 70.7MiB/s-70.7MiB/s (74.1MB/s-74.1MB/s), io=341MiB (358MB), run=4831-4831msec

使用mimalloc后反而更低了...
[32.3%][r=84KiB/s,w=124KiB/s][r=21,w=31 IOPS]

改为指数级malloc，而不是每次write都malloc，提升了约25%
   READ: bw=20.0MiB/s (21.0MB/s), 20.0MiB/s-20.0MiB/s (21.0MB/s-21.0MB/s), io=76.7MiB (80.5MB), run=3837-3837msec
  WRITE: bw=88.9MiB/s (93.3MB/s), 88.9MiB/s-88.9MiB/s (93.3MB/s-93.3MB/s), io=341MiB (358MB), run=3837-3837msec

ubuntu24.04 6.8.0-31-generic (之前是ubuntu22.04, 5.15.0-126-generic)
   READ: bw=48.7MiB/s (51.1MB/s), 48.7MiB/s-48.7MiB/s (51.1MB/s-51.1MB/s), io=255MiB (267MB), run=5230-5230msec
  WRITE: bw=97.9MiB/s (103MB/s), 97.9MiB/s-97.9MiB/s (103MB/s-103MB/s), io=512MiB (537MB), run=5230-5230msec

2025/01/11
上面的测试没有跑verify，跑verify后报错了，排查后原因可能有两点
1. 申请内存后没有初始化
2. write流程中size设置info->size = off + size; 但是不一定每次都是追加写，如果写前面的数据就会导致size不对，改为info->size = std::max(info->size, off + size);
   READ: bw=131MiB/s (137MB/s), 131MiB/s-131MiB/s (137MB/s-137MB/s), io=1024MiB (1074MB), run=7819-7819msec
  WRITE: bw=81.8MiB/s (85.7MB/s), 81.8MiB/s-81.8MiB/s (85.7MB/s-85.7MB/s), io=512MiB (537MB), run=6264-6264msec

**fio cmd**
fio -direct=1 -iodepth=32 -rw=randrw -ioengine=libaio -bs=1m -size=4G -numjobs=1 -runtime=30 -group_reporting -filename=/tmp/mfs/testfile -fallocate=none -name=RandRW -verify=md5 --do_verify=1 --verify_fatal=1

为了验证FUSE每笔写的最大限制，将bs改为1M，size改为4G
   READ: bw=439MiB/s (460MB/s), 439MiB/s-439MiB/s (460MB/s-460MB/s), io=4096MiB (4295MB), run=9330-9330msec
  WRITE: bw=388MiB/s (407MB/s), 388MiB/s-388MiB/s (407MB/s-407MB/s), io=2104MiB (2206MB), run=5422-5422msec

2025/01/13
把write流程中realloc逻辑优化了，原先while循环中每次capacity扩容，都会立即realloc，现在改为先确定扩容后的capacity，然后一次性realloc。对于吞吐实际没有提升。

2025/01/22
将全局变量files_[MAX_FILES]封装到FileManager中、增加OpenFile类，从而增加文件级别的锁，支持fuse多线程。线程数为4，读提升约10%，写提升约19%
   READ: bw=482MiB/s (505MB/s), 482MiB/s-482MiB/s (505MB/s-505MB/s), io=4096MiB (4295MB), run=8502-8502msec
  WRITE: bw=460MiB/s (482MB/s), 460MiB/s-460MiB/s (482MB/s-482MB/s), io=2104MiB (2206MB), run=4576-4576msec

将OpenFile的互斥锁改为读写锁，性能几乎没变化

2025/01/24

被柯总教育了，iodepth=1时，1M耗时250us，那吞吐应该是4000MB/s
通过top看到fio的CPU占用率为100%，说明校验消耗了大量CPU
关闭校验，重新测试

**fio cmd**
fio -direct=1 -iodepth=32 -rw=randrw -ioengine=libaio -bs=1m -size=4G -numjobs=1 -runtime=30 -time_based=1 -group_reporting -filename=/tmp/mfs/testfile -fallocate=none -name=RandRW

   READ: bw=3554MiB/s (3726MB/s), 3554MiB/s-3554MiB/s (3726MB/s-3726MB/s), io=104GiB (112GB), run=30001-30001msec
  WRITE: bw=3547MiB/s (3719MB/s), 3547MiB/s-3547MiB/s (3719MB/s-3719MB/s), io=104GiB (112GB), run=30001-30001msec

测试虚机内存
sysbench memory --memory-block-size=1M --memory-total-size=10G run
17525.85 MiB/sec