/*
 * Лабораторна робота №6, завдання 1.
 * Створення ієрархії файлів/папок та рекурсивний аналог команди tree.
 *
 * Компіляція: gcc -Wall -Wextra -o lab6_1 tree.c
 * Запуск:     ./lab6_1 [номер_за_журналом]     (за замовчуванням 11)
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Переносимість: у Windows (MinGW) mkdir() приймає лише один аргумент */
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

/* ---------- Створення ієрархії ---------- */

static void make_dir(const char *path)
{
    if (MKDIR(path) == -1 && errno != EEXIST) {
        fprintf(stderr, "Помилка mkdir(%s): %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static void make_file(const char *dir, const char *name, const char *content)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, name);

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Помилка fopen(%s): %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }
    fputs(content, f);
    fclose(f);
}

static void make_subdir(const char *root, const char *name, char *out)
{
    snprintf(out, PATH_MAX, "%s/%s", root, name);
    make_dir(out);
}

static void create_structure(const char *root)
{
    char git[PATH_MAX], src[PATH_MAX], bin[PATH_MAX];
    char docs[PATH_MAX], img[PATH_MAX];

    make_dir(root);

    make_subdir(root, ".git", git);
    make_file(git, "config", "[core]\n\trepositoryformatversion = 0\n");

    make_subdir(root, "src", src);
    make_file(src, "main.c", "#include <stdio.h>\n\nint main(void)\n{\n"
                             "    printf(\"Hello, world!\\n\");\n    return 0;\n}\n");

    make_subdir(root, "bin", bin);   /* порожня папка */

    make_subdir(root, "docs", docs);
    make_file(docs, "manual.txt", "Керівництво користувача.\n");
    make_file(docs, "presentation.pdf", "%PDF-1.4\n% демонстраційний файл\n");

    make_subdir(root, "img", img);
    make_file(img, "cat.jpg", "JPEG-заглушка\n");
    make_file(img, "dog.png", "PNG-заглушка\n");

    make_file(root, "README.md", "# lab6\n\nЛабораторна робота №6.\n");
}

/* ---------- Аналог tree ---------- */

static int cmp_names(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

/* Повертає 1, якщо шлях є директорією (використовує stat()). */
static int is_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1)
        return 0;
    return S_ISDIR(st.st_mode);
}

static void print_tree(const char *path, const char *prefix,
                       int *dirs, int *files)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "Помилка opendir(%s): %s\n", path, strerror(errno));
        return;
    }

    /* Збираємо імена, пропускаючи ".", ".." та приховані (починаються з '.') */
    char **names = NULL;
    size_t n = 0, cap = 0;
    struct dirent *e;

    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **tmp = realloc(names, cap * sizeof(char *));
            if (!tmp) {
                perror("realloc");
                break;
            }
            names = tmp;
        }
        names[n] = strdup(e->d_name);
        if (!names[n]) {
            perror("strdup");
            break;
        }
        n++;
    }
    closedir(d);

    qsort(names, n, sizeof(char *), cmp_names);

    for (size_t i = 0; i < n; i++) {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", path, names[i]);

        int last = (i == n - 1);
        printf("%s%s%s\n", prefix, last ? "└── " : "├── ", names[i]);

        if (is_directory(full)) {
            (*dirs)++;
            char new_prefix[PATH_MAX];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix,
                     last ? "    " : "│   ");
            print_tree(full, new_prefix, dirs, files);
        } else {
            (*files)++;
        }
        free(names[i]);
    }
    free(names);
}

int main(int argc, char *argv[])
{
    const char *number = (argc > 1) ? argv[1] : "11";
    char root[PATH_MAX];
    snprintf(root, sizeof(root), "lab6_%s", number);

    create_structure(root);

    int dirs = 0, files = 0;
    printf("%s\n", root);   /* стартову директорію виводимо завжди */
    print_tree(root, "", &dirs, &files);
    printf("\n%d directories, %d files\n", dirs, files);

    return 0;
}