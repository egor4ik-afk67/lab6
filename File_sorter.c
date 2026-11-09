/*
 * Лабораторна робота №6, завдання 2.
 * Утиліта file_sorter: рекурсивно сканує --source, сортує файли за
 * розширеннями в підпапки --dest, веде лог у sort_report.log.
 *
 * Компіляція: gcc -Wall -Wextra -o file_sorter file_sorter.c
 * Запуск:     ./file_sorter --source=lab6_11 --dest=out
 *             ./file_sorter --source lab6_11 --dest out
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Переносимість: Windows (MinGW) і Linux/macOS */
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) mkdir(p)
#define CANON(p, out) (_fullpath((out), (p), PATH_MAX) != NULL)
#else
#define MKDIR(p) mkdir(p, 0755)
#define CANON(p, out) (realpath((p), (out)) != NULL)
#endif

#define LOG_NAME "sort_report.log"

static FILE *g_log = NULL;
static char g_dest_real[PATH_MAX];   /* повний шлях --dest, щоб не сканувати його, якщо він усередині --source */
static int g_dest_known = 0;

/* ---------- Логування: консоль + файл ---------- */

static void log_msg(const char *fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);

    vprintf(fmt, ap);
    printf("\n");

    if (g_log) {
        vfprintf(g_log, fmt, ap2);
        fprintf(g_log, "\n");
        fflush(g_log);
    }
    va_end(ap2);
    va_end(ap);
}

/* ---------- Визначення категорії за розширенням ---------- */

static const char *category_of(const char *filename)
{
    static const struct { const char *ext; const char *cat; } map[] = {
        {".txt", "Documents"}, {".md", "Documents"},
        {".c", "Sources"}, {".h", "Sources"}, {".go", "Sources"},
        {".jpg", "Images"}, {".png", "Images"},
        {".pdf", "PDFs"},
    };

    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        return "Other";

    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (strcasecmp(dot, map[i].ext) == 0)
            return map[i].cat;

    return "Other";
}

/* ---------- Робота з директоріями ---------- */

/* Створює директорію (і батьківські, як mkdir -p). Логує створення. */
static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) ? 0 : -1;

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (MKDIR(tmp) == -1 && errno != EEXIST) {
                log_msg("Помилка: не вдалося створити папку %s: %s",
                        tmp, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }
    if (MKDIR(tmp) == -1 && errno != EEXIST) {
        log_msg("Помилка: не вдалося створити папку %s: %s", tmp, strerror(errno));
        return -1;
    }
    log_msg("Створено папку: %s", path);
    return 0;
}

/* Копіювання файлу (потрібне, якщо source і dest на різних файлових системах). */
static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in)
        return -1;
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    char buf[8192];
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            rc = -1;
            break;
        }
    }
    if (ferror(in))
        rc = -1;

    fclose(in);
    if (fclose(out) != 0)
        rc = -1;
    return rc;
}

/* Формує унікальний шлях призначення: name.ext -> name_1.ext, name_2.ext ... */
static void unique_target(const char *dir, const char *name, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", dir, name);

    struct stat st;
    if (stat(out, &st) == -1)
        return;

    const char *dot = strrchr(name, '.');
    int base_len = dot ? (int)(dot - name) : (int)strlen(name);
    const char *ext = dot ? dot : "";

    for (int i = 1; i < 100000; i++) {
        snprintf(out, size, "%s/%.*s_%d%s", dir, base_len, name, i, ext);
        if (stat(out, &st) == -1)
            return;
    }
}

static void move_file(const char *src_path, const char *name, const char *dest_root)
{
    const char *cat = category_of(name);

    char cat_dir[PATH_MAX];
    snprintf(cat_dir, sizeof(cat_dir), "%s/%s", dest_root, cat);
    if (ensure_dir(cat_dir) == -1) {
        log_msg("Помилка: пропущено %s (немає цільової папки)", src_path);
        return;
    }

    char target[PATH_MAX];
    unique_target(cat_dir, name, target, sizeof(target));

    if (rename(src_path, target) == 0) {
        log_msg("Переміщено: %s -> %s", src_path, target);
        return;
    }

    if (errno == EXDEV) {   /* різні файлові системи: копіюємо і видаляємо */
        if (copy_file(src_path, target) == 0 && remove(src_path) == 0) {
            log_msg("Переміщено: %s -> %s", src_path, target);
            return;
        }
    }
    log_msg("Помилка: не вдалося перемістити %s -> %s: %s",
            src_path, target, strerror(errno));
}

/* ---------- Рекурсивне сканування ---------- */

static int cmp_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void scan_dir(const char *path, const char *dest_root)
{
    DIR *d = opendir(path);
    if (!d) {
        log_msg("Помилка: не вдалося відкрити %s: %s", path, strerror(errno));
        return;
    }

    /* Спочатку збираємо імена, щоб переміщення не заважало readdir() */
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *e;

    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')        /* ".", "..", приховані файли/папки */
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **tmp = realloc(names, cap * sizeof(char *));
            if (!tmp) {
                log_msg("Помилка: недостатньо пам'яті");
                break;
            }
            names = tmp;
        }
        names[n] = strdup(e->d_name);
        if (!names[n])
            break;
        n++;
    }
    closedir(d);

    qsort(names, n, sizeof(char *), cmp_names);

    for (size_t i = 0; i < n; i++) {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", path, names[i]);

        struct stat st;
        if (stat(full, &st) == -1) {
            log_msg("Помилка: stat(%s): %s", full, strerror(errno));
        } else if (S_ISDIR(st.st_mode)) {
            char real[PATH_MAX];
            if (g_dest_known && CANON(full, real) &&
                strcasecmp(real, g_dest_real) == 0)
                ;   /* не заходимо в цільову папку */
            else
                scan_dir(full, dest_root);
        } else if (S_ISREG(st.st_mode)) {
            move_file(full, names[i], dest_root);
        }
        free(names[i]);
    }
    free(names);
}

/* ---------- Аргументи командного рядка ---------- */

static void usage(const char *prog)
{
    fprintf(stderr, "Використання: %s --source=<директорія> --dest=<директорія>\n"
                    "              %s --source <директорія> --dest <директорія>\n",
            prog, prog);
}

int main(int argc, char *argv[])
{
    const char *source = NULL, *dest = NULL;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--source=", 9) == 0)
            source = argv[i] + 9;
        else if (strncmp(argv[i], "--dest=", 7) == 0)
            dest = argv[i] + 7;
        else if (strcmp(argv[i], "--source") == 0 && i + 1 < argc)
            source = argv[++i];
        else if (strcmp(argv[i], "--dest") == 0 && i + 1 < argc)
            dest = argv[++i];
        else {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!source || !dest || !*source || !*dest) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    struct stat st;
    if (stat(source, &st) == -1 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Помилка: '%s' не є існуючою директорією\n", source);
        return EXIT_FAILURE;
    }

    g_log = fopen(LOG_NAME, "w");
    if (!g_log)
        fprintf(stderr, "Попередження: не вдалося створити %s: %s\n",
                LOG_NAME, strerror(errno));

    log_msg("--- Програма сортування запущена ---");

    if (ensure_dir(dest) == -1) {
        if (g_log)
            fclose(g_log);
        return EXIT_FAILURE;
    }
    if (CANON(dest, g_dest_real))
        g_dest_known = 1;

    scan_dir(source, dest);

    log_msg("Сортування завершено! Див. '%s'.", LOG_NAME);

    if (g_log)
        fclose(g_log);
    return EXIT_SUCCESS;
}