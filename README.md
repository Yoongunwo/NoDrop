# Execute for me

**Mode: external collector (monitor injection disabled).**
The module is loaded with `enable_monitor_injection=0`, so NoDrop does *not*
hijack application threads to flush their own buffers. Instead the kernel keeps
one 1 MB buffer per traced thread and a single external collector drains all of
them through `ioctl(NOD_IOCTL_FETCH_BUFFER)`. This makes NoDrop comparable to
the other baselines, which all use a separate collector with its own CPU budget;
with injection enabled there is no collector to give a budget to. See
[Notes](#notes) before reporting numbers.

1. Build

```
cd ~/NoDrop/build
cmake ..                      # 버퍼 키우려면: cmake .. -DBUFFER_SIZE=8*Mib
make ctrl                     # nodrop-dump도 같이 빌드됨 (add_dependencies)
make kmodule
cp ../scripts/ctrl/analyze_stream.sh ./scripts/ctrl/
```

2. NoDrop cgroup

```
sudo cgcreate -g cpu,cpuset:/nodrop_collector
echo 1 | sudo tee /sys/fs/cgroup/cpuset/nodrop_collector/cpuset.cpus
echo 0 | sudo tee /sys/fs/cgroup/cpuset/nodrop_collector/cpuset.mems

echo 10000 | sudo tee /sys/fs/cgroup/cpu/nodrop_collector/cpu.cfs_period_us
echo 300   | sudo tee /sys/fs/cgroup/cpu/nodrop_collector/cpu.cfs_quota_us

```

3. Workload cgroup

```
sudo cgcreate -g cpuset:/nodrop_workload
echo "2-$(($(nproc)-1))" | sudo tee /sys/fs/cgroup/cpuset/nodrop_workload/cpuset.cpus
echo 0 | sudo tee /sys/fs/cgroup/cpuset/nodrop_workload/cpuset.mems
```

4. Nodrop load

```
sudo rmmod nodrop 2>/dev/null || true
sudo insmod ./nodrop.ko target_comm=postmark enable_monitor_injection=0

./scripts/ctrl/ctrl bufsize 8192    # KB 단위 = 8MB/스레드
./scripts/ctrl/ctrl bufsize         # 확인
```

5. Nodrop collect

```
./scripts/ctrl/ctrl start
./scripts/ctrl/ctrl record normal

./scripts/ctrl/ctrl clear-stat
./scripts/ctrl/ctrl clean
./scripts/ctrl/ctrl stat > /tmp/stat_before.txt

# Collector: 단일 프로세스, 30ms 주기
sudo cgexec -g cpu,cpuset:nodrop_collector ./scripts/ctrl/ctrl stream 0.03 /tmp/nodrop &

# Workload (다른 코어에서)
sudo cgexec -g cpuset:nodrop_workload postmark ...
```

6. Kill

```
# After Collecting
sudo pkill -INT -f "ctrl stream"
sleep 1
./scripts/ctrl/ctrl stat > /tmp/stat_after.txt
./scripts/ctrl/analyze_stream.sh /tmp/nodrop/stream.idx /tmp/nodrop/stream.raw \
    /tmp/stat_before.txt /tmp/stat_after.txt
```

## Notes

**Collector budget vs. `cpuset` width.** The collector cgroup is pinned to a
single CPU (`cpuset.cpus=1`), so it cannot exceed 100% no matter what quota is
set. The budget scales as `3 x N`% with the number of application containers,
so once `N > 33` the quota must be spread over more than one CPU -- widen
`cpuset.cpus` accordingly, otherwise the collector is silently capped and the
drop rate reflects the `cpuset`, not the budget.

**`cfs_period_us`.** 10 ms / 0.3 ms is used instead of the more common
100 ms / 3 ms. Both are 3%, but with a 100 ms period the collector burns its
whole quota in 3 ms and is then frozen for 97 ms, which makes a 30 ms fetch
interval impossible to honour. Verify with
`cat /sys/fs/cgroup/cpu/nodrop_collector/cpu.stat` (`nr_throttled`,
`throttled_time`).

**Buffer size applies to new threads only.** `nod_buffer_size` is read in
`init_buffer()`, which runs when a thread is first traced. Run
`ctrl bufsize <KB>` *before* starting the workload; already-running threads keep
the buffer they were given. Total kernel memory is `buffer_size x traced
threads`, not per container.

**Log storage.** `postmark` is a filesystem benchmark, so putting `stream.raw`
on the same block device as its working directory makes the disk -- not the CPU
budget -- the limiting factor. Use a separate device for the audit log, and use
the same arrangement for every baseline. `/dev/shm` isolates CPU effects best
but consumes RAM proportional to the captured volume, so watch the size on long
runs.

**What the numbers mean.** With injection disabled the producer never blocks:
when the collector cannot keep up, buffers fill and events are lost, exactly as
in the eBPF-based baselines. `ctrl stat` before/after gives the loss
(`drop_unsolved`). Reported as `NoDrop_fetch`; the unmodified design
(`enable_monitor_injection=1`) trades those drops for application slowdown
instead and cannot be given an isolated collector budget.

# Auditing Frameworks Need Resource Isolation: A Systematic Study on the Super Producer Threat to System Auditing and Its Mitigation

Prototype source code for the NODROP research paper, presented at USENIX Security 2023. This paper is available at https://www.usenix.org/conference/usenixsecurity23/presentation/jiang-peng. If you find this repository useful, please cite our paper/repository.

We evaluated the event dropping, application performance slowdown and the running overhead of NODROP under different hardware configurations. Due to space limitations of the paper, we have included some results in **Appendix.pdf** in this GitHub repository as mentioned in our paper.

## Overview

Nodrop is a provenance collector which addresses the “data integrity vs. efficiency dilemma” without introducing significant extra overhead. It efficiently isolates resources for provenance data handling by enforcing processes to consume their own resource quota to handle the provenance data generated by themselves. NoDrop is inspired by the idea of threadlet. Instead of having independent threads to process system call events, NoDrop leverages the capability of other running threads. It dynamically instruments the processing logic to the memory of a running thread.

## Getting Started

NoDrop contains 2 major components: the kernel module and the monitor.

- **the kernel module**: codes under `kmodule/`. Nodrop-modules builds against a vanilla or distribution kernel, with no need for additional patches.
- **the monitor**: codes under `monitor/`.
  NoDrop is tested on Ubuntu 18.04 with unmodified Linux kernel 4.15.0-171.

See the [example of a quick start](<./docs/quick_start(cn).md>) for more detail.

### How to Install Nodrop

#### Environment requirements

- make
- CMake
- GCC/G++ > 8.0 (Linux) which supports '--static-pie' option
- pkg-config binary
- For Linux, the following kernel options must be enabled (usually they are, unless a custom built kernel is used):
  - `CONFIG_TRACEPOINTS`
  - `CONFIG_HAVE_SYSCALL_TRACEPOINTS`
- To get musl libc, just run the following command

```shell
 ./scripts/getmusl.sh <absolute-path-to-Nodrop>
```

- To get lua, just run the following command

```shell
 ./scripts/getlua.sh
```

- To get zlib, just run the following command

```shell
 ./scripts/getzlib.sh
```

#### Installation Instructions

```shell
mkdir build && cd build
cmake ..
make load
```

Then the kernel module is loaded. You can find the kernel module file called `nodrop.ko` in project root directory.

### Configuration

In NoDrop, there are 3 variables can be configured with cmake.

- `BUFFER_SIZE`: the size of each per-thread buffer (default value: 8MB)
- `MONITOR_PATH`: the path to find monitor executable (default value: `${PROJECT_BINARY_PATH}/monitor/monitor`)
- `STORE_PATH`: the pare that store the event data (default value: `/tmp/nodrop`)

When you generate cmake files, you can specify these variables. For example, if you want to set that buffer size is 4MB, monitor path is `/my/path/to/monitor` and store path is `/my/path/to/store`, you can run the following commands

```
cmake .. -DBUFFER_SIZE=8*Mib -DMONITOR_PATH=/my/path/to/monitor -DSTORE_PATH=/my/path/to/store
```

For buffer size, you can specify it with integer or using unit including Kib and Mib. For the above exmaple, you can also sepcify the buffer size using `-DBUFFER_SIZE=4096*1024`.

NoDrop also supports customed log path format. In default, the log path format in C-like is `STORE_PATH/%u-%ld.buf`, which accepts two arguments including thread id and ns-scale timestamp. This log path format can also be configured with CMake by defining a macro. For example, if you would like to specify the log path format to a tty device, the following commands will achieve this.

```
cmake .. -DPATH_FORMAT=/dev/pts/1
```

### Pkey Support

NoDrop utilizes Intel Protection Key (PKEY) to protect its memory. If your machine does not support pkey, you must disable it otherwise a SIGILL will triggered due to the illegel instruction used by pkey.

This option is enabled on default. To disable the pkey, you can instruct CMake with `-DPKEY_SUPPORT=off`.
