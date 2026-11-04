#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "lfs.h"

static uint8_t *g_img;
static size_t g_size;
static size_t g_progid;

static int bd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
    (void)c;
    memcpy(buffer, g_img + (size_t)block * 4096 + off, size);
    return 0;
}
static int bd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    (void)c;
    memcpy(g_img + (size_t)block * 4096 + off, buffer, size);
    return 0;
}
static int bd_erase(const struct lfs_config *c, lfs_block_t block) {
    (void)c;
    memset(g_img + (size_t)block * 4096, 0xFF, 4096);
    return 0;
}
static int bd_sync(const struct lfs_config *c) { (void)c; return 0; }

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <in_image> <out_image|-> <list|add> [src bin path...]\n", argv[0]);
        fprintf(stderr, "  the input image is opened read-only and never modified\n");
        fprintf(stderr, "  out_image '-' = read-only inspection, nothing is written\n");
        return 2;
    }
    const char *in_path = argv[1];
    const char *out_path = argv[2];
    const char *op = argv[3];
    FILE *f = fopen(in_path, "rb");
    if (!f) { perror("open input"); return 1; }
    fseek(f, 0, SEEK_END);
    g_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    g_img = malloc(g_size);
    if (fread(g_img, 1, g_size, f) != g_size) { perror("read"); return 1; }
    fclose(f);

    struct lfs_config cfg = {0};
    cfg.read = bd_read; cfg.prog = bd_prog; cfg.erase = bd_erase; cfg.sync = bd_sync;
    cfg.read_size = 256; cfg.prog_size = 256; cfg.block_size = 4096;
    cfg.block_count = (lfs_size_t)(g_size / 4096);
    cfg.block_cycles = 10000;
    cfg.cache_size = 256; cfg.lookahead_size = 3072;

    lfs_t lfs;
    int err = lfs_mount(&lfs, &cfg);
    if (err) { fprintf(stderr, "mount failed: %d\n", err); return 1; }
    printf("mounted, block_count=%u\n", cfg.block_count);

    if (strcmp(op, "list") == 0) {
        lfs_dir_t dir;
        struct lfs_info info;
        int r = lfs_dir_open(&lfs, &dir, "/");
        if (r) { fprintf(stderr, "dir open: %d\n", r); return 1; }
        while ((r = lfs_dir_read(&lfs, &dir, &info)) > 0) {
            printf("%c %8u  %s%s\n", info.type == LFS_TYPE_DIR ? 'd' : '-',
                   (unsigned)info.size, info.name, info.type == LFS_TYPE_DIR ? "/" : "");
            if (info.type == LFS_TYPE_DIR && strcmp(info.name, ".") && strcmp(info.name, "..")) {
                char sub[256];
                snprintf(sub, sizeof(sub), "/%s", info.name);
                lfs_dir_t d2; struct lfs_info i2;
                if (lfs_dir_open(&lfs, &d2, sub) == 0) {
                    while (lfs_dir_read(&lfs, &d2, &i2) > 0) {
                        if (strcmp(i2.name, ".") && strcmp(i2.name, ".."))
                            printf("    %8u  %s/%s\n", (unsigned)i2.size, info.name, i2.name);
                    }
                    lfs_dir_close(&lfs, &d2);
                }
            }
        }
        lfs_dir_close(&lfs, &dir);
    } else if (strcmp(op, "add") == 0) {
        if (argc < 6) {
            fprintf(stderr, "add requires at least one src/path pair\n");
            return 2;
        }
        for (int i = 4; i + 1 < argc; i += 2) {
            lfs_file_t fp;
            lfs_mkdir(&lfs, "/apps");
            int r = lfs_file_open(&lfs, &fp, argv[i + 1], LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
            if (r) { fprintf(stderr, "open %s: %d\n", argv[i + 1], r); return 1; }
            FILE *src = fopen(argv[i], "rb");
            if (!src) { perror("open src"); return 1; }
            static uint8_t buf[4096];
            size_t n;
            lfs_size_t total = 0;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                r = lfs_file_write(&lfs, &fp, buf, n);
                if (r < 0) { fprintf(stderr, "write: %d\n", r); return 1; }
                total += (lfs_size_t)n;
            }
            fclose(src);
            lfs_file_close(&lfs, &fp);
            printf("added %s -> %s (%u bytes)\n", argv[i], argv[i + 1], (unsigned)total);
        }
    } else {
        fprintf(stderr, "unknown op: %s\n", op);
        return 2;
    }

    err = lfs_unmount(&lfs);
    if (err) { fprintf(stderr, "unmount: %d\n", err); return 1; }

    if (strcmp(out_path, "-") != 0) {
        FILE *out = fopen(out_path, "wb");
        if (!out) { perror("open output"); return 1; }
        if (fwrite(g_img, 1, g_size, out) != g_size) { perror("write output"); return 1; }
        fclose(out);
        printf("result saved to %s\n", out_path);
    } else {
        printf("read-only inspection, no output written\n");
    }
    free(g_img);
    return 0;
}
