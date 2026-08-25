#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

static volatile sig_atomic_t stream_stopped = 0;
static void stream_stop(int sig) { (void)sig; stream_stopped = 1; }

#include "ioctl.h"

int main(int argc, char *argv[]) {
    int fd;
    int ret;
    FILE *file;
    unsigned long bufsize;
    struct buffer_count_info cinfo;
    struct fetch_buffer_struct fetch;
    struct nod_event_statistic nod_stat;
    struct stat lua_st;
    struct nod_lua_state lua_state;
    char lua_path[4096];
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [clean|fetch|stat|clear-stat|start|stop|count|record|bufsize (size in KB)|stream <interval_sec> <out_dir> [duration_sec]]\n", argv[0]);
        return 0;
    }

    fd = open(NOD_IOCTL_PATH, O_RDWR);
    if (fd < 0) {
        perror("Cannot open " NOD_IOCTL_PATH);
        return 127;
    }

    if (!strcmp(argv[1], "clean")) {
        if (!ioctl(fd, NOD_IOCTL_CLEAR_BUFFER, 0))
            fprintf(stderr, "Success\n");
    } else if (!strcmp(argv[1], "fetch")) {
        if ((ret = ioctl(fd, NOD_IOCTL_READ_BUFFER_COUNT_INFO, &cinfo))) {
            fprintf(stderr, "Get Buffer Count Info failed, reason %d\n", ret);
            return -1;
        }

        fetch.len = cinfo.unflushed_len;
        fetch.buf = malloc(fetch.len);
        if (!fetch.buf) {
            fprintf(stderr, "Allocate memory failed\n");
            return -1;
        }

        if ((ret = ioctl(fd, NOD_IOCTL_FETCH_BUFFER, &fetch))) {
            fprintf(stderr, "Fetch Buffer failed, reason %d\n", ret);
            return -1;
        }

        if (argc <= 2) file = stdout;
        else file = fopen(argv[2], "wb");
        if (!file) {
            fprintf(stderr, "Cannot open file\n");
            return -1;
        }

        if (fetch.len == 0) {
            fprintf(stderr, "No buffered events to fetch (0 bytes)\n");
        } else if (fwrite(fetch.buf, fetch.len, 1, file) == 1) {
            fprintf(stderr, "Write %lu bytes to file %s\n", fetch.len, argc <= 2 ? "stdout" : argv[2]);
        } else {
            fprintf(stderr, "Write to file %s failed\n", argc <= 2 ? "stdout" : argv[2]);
        }

        if (file != stdout)
            fclose(file);

    } else if (!strcmp(argv[1], "count")) {
        if (!ioctl(fd, NOD_IOCTL_READ_BUFFER_COUNT_INFO, &cinfo)) {
            printf("event_count=%lu,unflushed_count=%lu,unflushed_len=%lu\n", cinfo.event_count, cinfo.unflushed_count, cinfo.unflushed_len);
        }
    } else if (!strcmp(argv[1], "stat")) {
      if (!ioctl(fd, NOD_IOCTL_READ_STATISTICS, &nod_stat)) {
          printf("n_evts\tdrop_evts\tdrop_unsolved\n%ld\t%ld\t%ld\n", nod_stat.n_evts, nod_stat.n_drop_evts, nod_stat.n_drop_evts_unsolved);
      }
    } else if (!strcmp(argv[1], "clear-stat")) {
      if (!ioctl(fd, NOD_IOCTL_CLEAR_STATISTICS, 0)) {
        fprintf(stderr, "Statistics cleared\n");
      }
    } else if (!strcmp(argv[1], "stop")) {
        if (!ioctl(fd, NOD_IOCTL_STOP_RECORDING, 0))
            fprintf(stderr, "Stopped\n");

    } else if (!strcmp(argv[1], "start")) {
        if (argc > 3) {
            fprintf(stderr, "Usage: %s start <lua_path>\n", argv[0]);
            return -1;
        }
        if (argc == 3) {
            if (!realpath(argv[2], lua_path)) {
                fprintf(stderr, "%s : lua path error\n", argv[2]);
                return -1;
            }
            if (strlen(lua_path) + 1 > 256) {
                fprintf(stderr, "%s : lua path too long\n", lua_path);
                return -1;
            }
            strcpy(lua_state.lua_path, lua_path);
            if (stat(lua_state.lua_path, &lua_st)) {
                fprintf(stderr, "%s : lua file error\n", lua_path);
                return -1;
            }
            lua_state.lua_mtime = lua_st.st_mtime;
        } else {
            lua_state.lua_path[0] = '\0';
            lua_state.lua_mtime = 0;
        }
        if (!ioctl(fd, NOD_IOCTL_START_RECORDING, 0) && !ioctl(fd, NOD_IOCTL_SET_LUA_STATE, &lua_state)) {
            if (argc == 3)
                fprintf(stderr, "Start with lua: %s\n", lua_state.lua_path);
            else 
                fprintf(stderr, "Start without lua\n");
        }
    } else if (!strcmp(argv[1], "bufsize")) {
        if (argc >= 3) {
            bufsize = (unsigned long)atol(argv[2]);
            bufsize *= 1024;
            if ((ret = ioctl(fd, NOD_IOCTL_SET_BUFFER_SIZE, bufsize))) {
                fprintf(stderr, "set buffer size failed: %d\n", ret);
            }
        }
        if ((ret = ioctl(fd, NOD_IOCTL_GET_BUFFER_SIZE, &bufsize))) {
            fprintf(stderr, "get buffer size failed: %d\n", ret);
            return -1;
        }
        printf("buffer size: %lu\n", bufsize);
    } else if (!strcmp(argv[1], "record")) {
        if (argc > 3) {
            fprintf(stderr, "Usage: %s record [normal, compress, none] (default normal)\n", argv[0]);
            return -1;
        }
        int record_flag = NOD_RECORD_MODE_START;
        if (argc == 3) {
            if (!strcmp(argv[2], "none"))
                record_flag = NOD_RECORD_MODE_STOP;
            else if (!strcmp(argv[2], "normal"))
                record_flag = NOD_RECORD_MODE_START;
            else if (!strcmp(argv[2], "compress"))
                record_flag = NOD_RECORD_MODE_COMPRESS;
            else {
                fprintf(stderr, "Usage: %s record [normal, compress, none] (default normal)\n", argv[0]);
                return -1;
            }
        }
        if (!ioctl(fd, NOD_IOCTL_SET_RECORD_FLAG, &record_flag)) {
            fprintf(stderr, "Record set %s\n", record_flag==0 ? "none": record_flag == 1 ? "normal" : "compress");
        }
    } else if (!strcmp(argv[1], "stream")) {
        /*
         * Self-contained fetch loop: opens the device once and drains every
         * per-thread buffer on a fixed interval, without forking a process per
         * iteration. Emits the same stream.raw / stream.idx pair that
         * analyze_stream.sh expects:
         *   fetch_start_ns fetch_end_ns events bytes raw_off_start raw_off_end
         *
         * Usage: ctrl stream [interval_sec] [out_dir] [duration_sec]
         */
        double interval = argc > 2 ? atof(argv[2]) : 0.2;
        const char *out_dir = argc > 3 ? argv[3] : "/tmp/nodrop";
        double duration = argc > 4 ? atof(argv[4]) : 0.0;
        char raw_path[4096], idx_path[4096];
        FILE *raw_fp, *idx_fp;
        char *buf = NULL;
        size_t buf_cap = 0;
        unsigned long long raw_off = 0;
        struct timespec req, t0, t1, tstart, now;

        if (interval <= 0)
            interval = 0.2;
        snprintf(raw_path, sizeof(raw_path), "%s/stream.raw", out_dir);
        snprintf(idx_path, sizeof(idx_path), "%s/stream.idx", out_dir);
        mkdir(out_dir, 0755);
        raw_fp = fopen(raw_path, "wb");
        idx_fp = fopen(idx_path, "w");
        if (!raw_fp || !idx_fp) {
            perror("Cannot open output files");
            return -1;
        }

        signal(SIGINT, stream_stop);
        signal(SIGTERM, stream_stop);

        req.tv_sec = (time_t)interval;
        req.tv_nsec = (long)((interval - (double)req.tv_sec) * 1e9);

        fprintf(stderr, "[stream] interval=%.4fs raw=%s idx=%s\n", interval, raw_path, idx_path);
        clock_gettime(CLOCK_MONOTONIC, &tstart);

        while (!stream_stopped) {
            if (ioctl(fd, NOD_IOCTL_READ_BUFFER_COUNT_INFO, &cinfo)) {
                fprintf(stderr, "Get Buffer Count Info failed\n");
                break;
            }

            if (cinfo.unflushed_len > 0) {
                /*
                 * Ask for more than the last observed length: events keep
                 * arriving between the count and the fetch, and the kernel
                 * stops copying at the first thread buffer that no longer
                 * fits (see __proc_buf_copy).
                 */
                size_t need = (size_t)cinfo.unflushed_len + (4 << 20);
                if (need > buf_cap) {
                    char *nbuf = realloc(buf, need);
                    if (!nbuf) {
                        fprintf(stderr, "Allocate memory failed (%zu bytes)\n", need);
                        break;
                    }
                    buf = nbuf;
                    buf_cap = need;
                }

                fetch.len = buf_cap;
                fetch.buf = buf;
                clock_gettime(CLOCK_REALTIME, &t0);
                ret = ioctl(fd, NOD_IOCTL_FETCH_BUFFER, &fetch);
                clock_gettime(CLOCK_REALTIME, &t1);
                if (ret) {
                    fprintf(stderr, "Fetch Buffer failed, reason %d\n", ret);
                    break;
                }

                if (fetch.len > 0) {
                    if (fwrite(buf, 1, fetch.len, raw_fp) != fetch.len) {
                        perror("Write to stream.raw failed");
                        break;
                    }
                    fprintf(idx_fp, "%llu %llu %llu %llu %llu %llu\n",
                            (unsigned long long)t0.tv_sec * 1000000000ULL + (unsigned long long)t0.tv_nsec,
                            (unsigned long long)t1.tv_sec * 1000000000ULL + (unsigned long long)t1.tv_nsec,
                            (unsigned long long)cinfo.unflushed_count,
                            (unsigned long long)fetch.len,
                            raw_off, raw_off + (unsigned long long)fetch.len);
                    raw_off += (unsigned long long)fetch.len;
                    fflush(raw_fp);
                    fflush(idx_fp);
                }
            }

            if (duration > 0) {
                clock_gettime(CLOCK_MONOTONIC, &now);
                if ((now.tv_sec - tstart.tv_sec) + (now.tv_nsec - tstart.tv_nsec) / 1e9 >= duration)
                    break;
            }

            nanosleep(&req, NULL);
        }

        fprintf(stderr, "[stream] stopped, %llu bytes in %s\n", raw_off, raw_path);
        fclose(raw_fp);
        fclose(idx_fp);
        free(buf);
    } else {
        fprintf(stderr, "Unknown cmd %s\n", argv[1]);
    }

    return 0;
}
