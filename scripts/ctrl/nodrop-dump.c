#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

#include "events.h"

static const char *print_format[PT_UINT64 + 1][PF_OCT + 1] = {
    [PT_NONE] = {"", "", "", "", ""},
    [PT_INT8] = {"", "%" PRId8, "0x%" PRIx8, "%010" PRId8, "0%" PRIo8},
    [PT_INT16] = {"", "%" PRId16, "0x%" PRIx16, "%010" PRId16, "0%" PRIo16},
    [PT_INT32] = {"", "%" PRId32, "0x%" PRIx32, "%010" PRId32, "0%" PRIo32},
    [PT_INT64] = {"", "%" PRId64, "0x%" PRIx64, "%010" PRId64, "0%" PRIo64},
    [PT_UINT8] = {"", "%" PRIu8, "0x%" PRIx8, "%010" PRIu8, "0%" PRIo8},
    [PT_UINT16] = {"", "%" PRIu16, "0x%" PRIx16, "%010" PRIu16, "0%" PRIo16},
    [PT_UINT32] = {"", "%" PRIu32, "0x%" PRIx32, "%010" PRIu32, "0%" PRIo32},
    [PT_UINT64] = {"", "%" PRIu64, "0x%" PRIx64, "%010" PRIu64, "0%" PRIo64}
};

static void print_escaped_text(const uint8_t *buf, size_t len)
{
    size_t i;
    fputc('"', stdout);
    for (i = 0; i < len; i++) {
        unsigned char c = buf[i];
        if (c == '\0') {
            break;
        }
        if (c == '\\' || c == '"') {
            fputc('\\', stdout);
            fputc(c, stdout);
        } else if (isprint(c)) {
            fputc(c, stdout);
        } else {
            fprintf(stdout, "\\x%02x", c);
        }
    }
    fputc('"', stdout);
}

static void print_hex_preview(const uint8_t *buf, size_t len)
{
    size_t i;
    size_t n = len > 16 ? 16 : len;
    fputs("0x", stdout);
    for (i = 0; i < n; i++) {
        fprintf(stdout, "%02x", buf[i]);
    }
    if (len > n) {
        fputs("...", stdout);
    }
}

static int parse_one_event(const uint8_t *ev, size_t ev_len, uint64_t index)
{
    uint32_t i;
    const struct nod_event_hdr *hdr = (const struct nod_event_hdr *)ev;
    const struct nod_event_info *info;
    const uint16_t *arg_sizes;
    const uint8_t *data;
    const uint8_t *data_end;
    const uint8_t *ptr;
    size_t payload_len;
    size_t header_need;

    if (ev_len < sizeof(*hdr)) {
        fprintf(stderr, "event #%" PRIu64 ": too short header (%zu)\n", index, ev_len);
        return -1;
    }
    if (hdr->magic != NOD_EVENT_HDR_MAGIC) {
        fprintf(stderr, "event #%" PRIu64 ": bad magic 0x%08x\n", index, hdr->magic);
        return -1;
    }
    if (hdr->len < sizeof(*hdr) || hdr->len > ev_len) {
        fprintf(stderr, "event #%" PRIu64 ": invalid len %u (remaining %zu)\n",
                index, hdr->len, ev_len);
        return -1;
    }
    if (hdr->type >= NODE_EVENT_MAX) {
        fprintf(stderr, "event #%" PRIu64 ": invalid type %u\n", index, hdr->type);
        return -1;
    }

    info = &g_event_info[hdr->type];
    payload_len = hdr->len - sizeof(*hdr);
    header_need = (size_t)info->nparams * sizeof(uint16_t);
    if (payload_len < header_need) {
        fprintf(stderr, "event #%" PRIu64 ": payload too short (%zu < %zu)\n",
                index, payload_len, header_need);
        return -1;
    }

    arg_sizes = (const uint16_t *)(ev + sizeof(*hdr));
    data = ev + sizeof(*hdr) + header_need;
    data_end = ev + hdr->len;
    ptr = data;

    printf("%" PRIu64 " tid=%u cpu=%u type=%u %s(",
           (uint64_t)hdr->ts, hdr->tid, hdr->cpuid, hdr->type, info->name);

    for (i = 0; i < info->nparams; i++) {
        const struct nod_param_info *param = &info->params[i];
        uint16_t sz = arg_sizes[i];

        if (i > 0) {
            fputs(", ", stdout);
        }
        printf("%s=", param->name);

        if (ptr + sz > data_end) {
            printf("<truncated:%u>", sz);
            break;
        }

        switch (param->type) {
        case PT_CHARBUF:
        case PT_FSPATH:
        case PT_FSRELPATH:
            print_escaped_text(ptr, sz);
            break;
        case PT_BYTEBUF:
            print_hex_preview(ptr, sz);
            break;
        case PT_FLAGS8:
        case PT_UINT8:
        case PT_SIGTYPE:
            if (sz >= sizeof(uint8_t)) {
                printf(print_format[PT_UINT8][param->fmt], *(const uint8_t *)ptr);
            } else {
                fputs("<short>", stdout);
            }
            break;
        case PT_FLAGS16:
        case PT_UINT16:
        case PT_SYSCALLID:
            if (sz >= sizeof(uint16_t)) {
                printf(print_format[PT_UINT16][param->fmt], *(const uint16_t *)ptr);
            } else {
                fputs("<short>", stdout);
            }
            break;
        case PT_FLAGS32:
        case PT_UINT32:
        case PT_MODE:
        case PT_UID:
        case PT_GID:
        case PT_SIGSET:
            if (sz >= sizeof(uint32_t)) {
                printf(print_format[PT_UINT32][param->fmt], *(const uint32_t *)ptr);
            } else {
                fputs("<short>", stdout);
            }
            break;
        case PT_RELTIME:
        case PT_ABSTIME:
        case PT_UINT64:
            if (sz >= sizeof(uint64_t)) {
                printf(print_format[PT_UINT64][param->fmt], *(const uint64_t *)ptr);
            } else {
                fputs("<short>", stdout);
            }
            break;
        case PT_INT8:
            if (sz >= sizeof(int8_t)) {
                printf(print_format[PT_INT8][param->fmt], *(const int8_t *)ptr);
            } else {
                fputs("<short>", stdout);
            }
            break;
        case PT_INT16:
            if (sz >= sizeof(int16_t)) {
                printf(print_format[PT_INT16][param->fmt], *(const int16_t *)ptr);
            } else {
                fputs("<short>", stdout);
            }
            break;
        case PT_INT32:
            if (sz >= sizeof(int32_t)) {
                printf(print_format[PT_INT32][param->fmt], *(const int32_t *)ptr);
            } else {
                fputs("<short>", stdout);
            }
            break;
        case PT_INT64:
        case PT_ERRNO:
        case PT_FD:
        case PT_PID:
            if (sz >= sizeof(int64_t)) {
                printf(print_format[PT_INT64][param->fmt], *(const int64_t *)ptr);
            } else {
                fputs("<short>", stdout);
            }
            break;
        default:
            print_hex_preview(ptr, sz);
            break;
        }

        ptr += sz;
    }

    fputs(")\n", stdout);
    return 0;
}

int main(int argc, char **argv)
{
    FILE *fp;
    uint8_t *buf;
    long fsize;
    size_t nread;
    size_t off = 0;
    uint64_t index = 0;
    int errors = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <nodrop_raw_file>\n", argv[0]);
        return 2;
    }

    fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        return 1;
    }
    fsize = ftell(fp);
    if (fsize < 0) {
        perror("ftell");
        fclose(fp);
        return 1;
    }
    rewind(fp);

    buf = (uint8_t *)malloc((size_t)fsize);
    if (!buf) {
        fprintf(stderr, "malloc failed\n");
        fclose(fp);
        return 1;
    }

    nread = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    if (nread != (size_t)fsize) {
        fprintf(stderr, "short read: %zu/%ld\n", nread, fsize);
        free(buf);
        return 1;
    }

    while (off < nread) {
        const struct nod_event_hdr *hdr;
        size_t remaining = nread - off;
        if (remaining < sizeof(*hdr)) {
            fprintf(stderr, "tail bytes not enough for header: %zu\n", remaining);
            errors++;
            break;
        }

        hdr = (const struct nod_event_hdr *)(buf + off);
        if (hdr->len == 0 || hdr->len > remaining) {
            fprintf(stderr, "event #%" PRIu64 ": invalid len %u at offset %zu\n",
                    index, hdr->len, off);
            errors++;
            break;
        }

        if (parse_one_event(buf + off, remaining, index) != 0) {
            errors++;
        }

        off += hdr->len;
        index++;
    }

    fprintf(stderr, "parsed events=%" PRIu64 ", errors=%d\n", index, errors);
    free(buf);
    return errors ? 1 : 0;
}

