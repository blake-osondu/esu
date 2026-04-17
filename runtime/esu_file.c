#include "esu_file.h"
#include "esu_list.h"
#include "esu_error.h"
#include "esu_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

/* Expand ~ at start and $VAR / ${VAR} anywhere in a path string.
 * Returns a malloc'd string (caller must esu_free). */
static char *expand_path_str(const char *src) {
    int cap = 256;
    char *out = esu_alloc(cap);
    int len = 0;
    int i = 0;

    /* Handle leading ~ expansion */
    if (src[0] == '~' && (src[1] == '/' || src[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            int hlen = strlen(home);
            while (len + hlen + 1 > cap) { cap *= 2; out = esu_realloc(out, cap); }
            memcpy(out + len, home, hlen);
            len += hlen;
            i = 1; /* skip the ~ */
        }
    }

    while (src[i]) {
        if (src[i] == '$') {
            /* Variable expansion: $VAR or ${VAR} */
            int start = i + 1;
            int has_brace = 0;
            if (src[start] == '{') { has_brace = 1; start++; }
            int var_end = start;
            while (src[var_end] && (isalnum((unsigned char)src[var_end]) || src[var_end] == '_')) {
                var_end++;
            }
            if (var_end > start) {
                /* Extract var name */
                int name_len = var_end - start;
                char name_buf[256];
                if (name_len >= (int)sizeof(name_buf)) name_len = sizeof(name_buf) - 1;
                memcpy(name_buf, src + start, name_len);
                name_buf[name_len] = '\0';
                const char *val = getenv(name_buf);
                if (val) {
                    int vlen = strlen(val);
                    while (len + vlen + 1 > cap) { cap *= 2; out = esu_realloc(out, cap); }
                    memcpy(out + len, val, vlen);
                    len += vlen;
                }
                i = var_end;
                if (has_brace && src[i] == '}') i++;
                continue;
            }
        }
        if (len + 2 > cap) { cap *= 2; out = esu_realloc(out, cap); }
        out[len++] = src[i++];
    }
    out[len] = '\0';
    return out;
}


EsuValue *esu_file_read(EsuValue *path) {
    if (path->type != VAL_STRING) {
        esu_raise_msg("file path must be a string");
        return esu_string("");
    }
    char *p = expand_path_str(path->as.string);
    FILE *f = fopen(p, "rb");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot open file: %s", p);
        esu_free(p);
        esu_raise_msg(msg);
        return esu_string("");
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = esu_alloc(size + 1);
    size_t got = fread(buf, 1, size, f);
    buf[got] = '\0';
    fclose(f);
    EsuValue *result = esu_string(buf);
    esu_free(buf);
    esu_free(p);
    return result;
}

EsuValue *esu_file_write(EsuValue *path, EsuValue *content) {
    if (path->type != VAL_STRING) {
        esu_raise_msg("file path must be a string");
        return esu_nothing();
    }
    char *p = expand_path_str(path->as.string);
    char *s = esu_to_cstring(content);
    FILE *f = fopen(p, "wb");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot write to file: %s", p);
        esu_free(p);
        esu_free(s);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    fwrite(s, 1, strlen(s), f);
    fclose(f);
    esu_free(p);
    esu_free(s);
    return esu_nothing();
}

EsuValue *esu_file_append(EsuValue *path, EsuValue *content) {
    if (path->type != VAL_STRING) {
        esu_raise_msg("file path must be a string");
        return esu_nothing();
    }
    char *p = expand_path_str(path->as.string);
    FILE *f = fopen(p, "ab");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot append to file: %s", p);
        esu_free(p);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    char *s = esu_to_cstring(content);
    fwrite(s, 1, strlen(s), f);
    fclose(f);
    esu_free(s);
    esu_free(p);
    return esu_nothing();
}

EsuValue *esu_file_exists(EsuValue *path) {
    if (path->type != VAL_STRING) return esu_bool(0);
    char *p = expand_path_str(path->as.string);
    int exists = (access(p, F_OK) == 0);
    esu_free(p);
    return esu_bool(exists);
}

EsuValue *esu_file_delete(EsuValue *path) {
    if (path->type != VAL_STRING) {
        esu_raise_msg("file path must be a string");
        return esu_nothing();
    }
    char *p = expand_path_str(path->as.string);
    int result = remove(p);
    if (result != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot delete file: %s", p);
        esu_free(p);
        esu_raise_msg(msg);
    }
    esu_free(p);
    return esu_nothing();
}

EsuValue *esu_file_create(EsuValue *path) {
    if (path->type != VAL_STRING) { esu_raise_msg("file path must be a string"); return esu_nothing(); }
    char *p = expand_path_str(path->as.string);
    if (access(p, F_OK) == 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "file already exists: %s", p);
        esu_free(p);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    FILE *f = fopen(p, "wb");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot create file: %s", p);
        esu_free(p);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    fclose(f);
    esu_free(p);
    return esu_nothing();
}

EsuValue *esu_file_touch(EsuValue *path) {
    if (path->type != VAL_STRING) { esu_raise_msg("file path must be a string"); return esu_nothing(); }
    char *p = expand_path_str(path->as.string);
    FILE *f = fopen(p, "ab");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot touch file: %s", p);
        esu_free(p);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    fclose(f);
    esu_free(p);
    return esu_nothing();
}

EsuValue *esu_file_move(EsuValue *from, EsuValue *to) {
    if (from->type != VAL_STRING || to->type != VAL_STRING) {
        esu_raise_msg("file paths must be strings");
        return esu_nothing();
    }
    char *f = expand_path_str(from->as.string);
    char *t = expand_path_str(to->as.string);
    if (rename(f, t) != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot move file from %s to %s", f, t);
        esu_free(f); esu_free(t);
        esu_raise_msg(msg);
    }
    esu_free(f); esu_free(t);
    return esu_nothing();
}

EsuValue *esu_file_copy(EsuValue *from, EsuValue *to) {
    if (from->type != VAL_STRING || to->type != VAL_STRING) {
        esu_raise_msg("file paths must be strings");
        return esu_nothing();
    }
    char *fp = expand_path_str(from->as.string);
    char *tp = expand_path_str(to->as.string);
    FILE *src = fopen(fp, "rb");
    if (!src) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot read source file: %s", fp);
        esu_free(fp); esu_free(tp);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    FILE *dst = fopen(tp, "wb");
    if (!dst) {
        fclose(src);
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot write to destination: %s", tp);
        esu_free(fp); esu_free(tp);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }
    fclose(src);
    fclose(dst);
    esu_free(fp); esu_free(tp);
    return esu_nothing();
}

/* Path builtins */

EsuValue *esu_home_dir(void) {
    const char *h = getenv("HOME");
    return esu_string(h ? h : "/");
}

EsuValue *esu_current_dir(void) {
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) return esu_string(buf);
    return esu_string(".");
}

EsuValue *esu_temp_dir(void) {
    const char *t = getenv("TMPDIR");
    if (!t) t = getenv("TMP");
    if (!t) t = "/tmp";
    int len = strlen(t);
    if (len > 1 && t[len - 1] == '/') {
        char *buf = esu_alloc(len);
        memcpy(buf, t, len - 1);
        buf[len - 1] = '\0';
        EsuValue *v = esu_string(buf);
        esu_free(buf);
        return v;
    }
    return esu_string(t);
}

static EsuValue *home_subdir(const char *sub) {
    const char *h = getenv("HOME");
    if (!h) return esu_string(sub);
    int hlen = strlen(h);
    int slen = strlen(sub);
    char *buf = esu_alloc(hlen + slen + 2);
    memcpy(buf, h, hlen);
    buf[hlen] = '/';
    memcpy(buf + hlen + 1, sub, slen);
    buf[hlen + 1 + slen] = '\0';
    EsuValue *v = esu_string(buf);
    esu_free(buf);
    return v;
}

EsuValue *esu_desktop_dir(void)   { return home_subdir("Desktop"); }
EsuValue *esu_downloads_dir(void) { return home_subdir("Downloads"); }
EsuValue *esu_documents_dir(void) { return home_subdir("Documents"); }

EsuValue *esu_expand_path(EsuValue *path) {
    if (path->type != VAL_STRING) return esu_retain(path);
    char *e = expand_path_str(path->as.string);
    EsuValue *result = esu_string(e);
    esu_free(e);
    return result;
}

EsuValue *esu_file_lines(EsuValue *path) {
    EsuValue *content = esu_file_read(path);
    EsuValue *result = esu_list_new();
    if (content->type != VAL_STRING) {
        esu_release(content);
        return result;
    }
    const char *s = content->as.string;
    const char *line_start = s;
    while (*s) {
        if (*s == '\n') {
            int len = s - line_start;
            char *buf = esu_alloc(len + 1);
            memcpy(buf, line_start, len);
            buf[len] = '\0';
            EsuValue *line_val = esu_string(buf);
            esu_list_append(result, line_val);
            esu_release(line_val);
            esu_free(buf);
            line_start = s + 1;
        }
        s++;
    }
    /* Last line without trailing newline */
    if (s > line_start) {
        int len = s - line_start;
        char *buf = esu_alloc(len + 1);
        memcpy(buf, line_start, len);
        buf[len] = '\0';
        EsuValue *line_val = esu_string(buf);
        esu_list_append(result, line_val);
        esu_release(line_val);
        esu_free(buf);
    }
    esu_release(content);
    return result;
}
