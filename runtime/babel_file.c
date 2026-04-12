#include "babel_file.h"
#include "babel_list.h"
#include "babel_error.h"
#include "babel_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

/* Expand ~ at start and $VAR / ${VAR} anywhere in a path string.
 * Returns a malloc'd string (caller must babel_free). */
static char *expand_path_str(const char *src) {
    int cap = 256;
    char *out = babel_alloc(cap);
    int len = 0;
    int i = 0;

    /* Handle leading ~ expansion */
    if (src[0] == '~' && (src[1] == '/' || src[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            int hlen = strlen(home);
            while (len + hlen + 1 > cap) { cap *= 2; out = babel_realloc(out, cap); }
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
                    while (len + vlen + 1 > cap) { cap *= 2; out = babel_realloc(out, cap); }
                    memcpy(out + len, val, vlen);
                    len += vlen;
                }
                i = var_end;
                if (has_brace && src[i] == '}') i++;
                continue;
            }
        }
        if (len + 2 > cap) { cap *= 2; out = babel_realloc(out, cap); }
        out[len++] = src[i++];
    }
    out[len] = '\0';
    return out;
}


BabelValue *babel_file_read(BabelValue *path) {
    if (path->type != VAL_STRING) {
        babel_raise_msg("file path must be a string");
        return babel_string("");
    }
    char *p = expand_path_str(path->as.string);
    FILE *f = fopen(p, "rb");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot open file: %s", p);
        babel_free(p);
        babel_raise_msg(msg);
        return babel_string("");
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = babel_alloc(size + 1);
    size_t got = fread(buf, 1, size, f);
    buf[got] = '\0';
    fclose(f);
    BabelValue *result = babel_string(buf);
    babel_free(buf);
    babel_free(p);
    return result;
}

BabelValue *babel_file_write(BabelValue *path, BabelValue *content) {
    if (path->type != VAL_STRING) {
        babel_raise_msg("file path must be a string");
        return babel_nothing();
    }
    char *p = expand_path_str(path->as.string);
    char *s = babel_to_cstring(content);
    FILE *f = fopen(p, "wb");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot write to file: %s", p);
        babel_free(p);
        babel_free(s);
        babel_raise_msg(msg);
        return babel_nothing();
    }
    fwrite(s, 1, strlen(s), f);
    fclose(f);
    babel_free(p);
    babel_free(s);
    return babel_nothing();
}

BabelValue *babel_file_append(BabelValue *path, BabelValue *content) {
    if (path->type != VAL_STRING) {
        babel_raise_msg("file path must be a string");
        return babel_nothing();
    }
    char *p = expand_path_str(path->as.string);
    FILE *f = fopen(p, "ab");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot append to file: %s", p);
        babel_free(p);
        babel_raise_msg(msg);
        return babel_nothing();
    }
    char *s = babel_to_cstring(content);
    fwrite(s, 1, strlen(s), f);
    fclose(f);
    babel_free(s);
    babel_free(p);
    return babel_nothing();
}

BabelValue *babel_file_exists(BabelValue *path) {
    if (path->type != VAL_STRING) return babel_bool(0);
    char *p = expand_path_str(path->as.string);
    int exists = (access(p, F_OK) == 0);
    babel_free(p);
    return babel_bool(exists);
}

BabelValue *babel_file_delete(BabelValue *path) {
    if (path->type != VAL_STRING) {
        babel_raise_msg("file path must be a string");
        return babel_nothing();
    }
    char *p = expand_path_str(path->as.string);
    int result = remove(p);
    if (result != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot delete file: %s", p);
        babel_free(p);
        babel_raise_msg(msg);
    }
    babel_free(p);
    return babel_nothing();
}

BabelValue *babel_file_create(BabelValue *path) {
    if (path->type != VAL_STRING) { babel_raise_msg("file path must be a string"); return babel_nothing(); }
    char *p = expand_path_str(path->as.string);
    if (access(p, F_OK) == 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "file already exists: %s", p);
        babel_free(p);
        babel_raise_msg(msg);
        return babel_nothing();
    }
    FILE *f = fopen(p, "wb");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot create file: %s", p);
        babel_free(p);
        babel_raise_msg(msg);
        return babel_nothing();
    }
    fclose(f);
    babel_free(p);
    return babel_nothing();
}

BabelValue *babel_file_touch(BabelValue *path) {
    if (path->type != VAL_STRING) { babel_raise_msg("file path must be a string"); return babel_nothing(); }
    char *p = expand_path_str(path->as.string);
    FILE *f = fopen(p, "ab");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot touch file: %s", p);
        babel_free(p);
        babel_raise_msg(msg);
        return babel_nothing();
    }
    fclose(f);
    babel_free(p);
    return babel_nothing();
}

BabelValue *babel_file_move(BabelValue *from, BabelValue *to) {
    if (from->type != VAL_STRING || to->type != VAL_STRING) {
        babel_raise_msg("file paths must be strings");
        return babel_nothing();
    }
    char *f = expand_path_str(from->as.string);
    char *t = expand_path_str(to->as.string);
    if (rename(f, t) != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot move file from %s to %s", f, t);
        babel_free(f); babel_free(t);
        babel_raise_msg(msg);
    }
    babel_free(f); babel_free(t);
    return babel_nothing();
}

BabelValue *babel_file_copy(BabelValue *from, BabelValue *to) {
    if (from->type != VAL_STRING || to->type != VAL_STRING) {
        babel_raise_msg("file paths must be strings");
        return babel_nothing();
    }
    char *fp = expand_path_str(from->as.string);
    char *tp = expand_path_str(to->as.string);
    FILE *src = fopen(fp, "rb");
    if (!src) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot read source file: %s", fp);
        babel_free(fp); babel_free(tp);
        babel_raise_msg(msg);
        return babel_nothing();
    }
    FILE *dst = fopen(tp, "wb");
    if (!dst) {
        fclose(src);
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot write to destination: %s", tp);
        babel_free(fp); babel_free(tp);
        babel_raise_msg(msg);
        return babel_nothing();
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }
    fclose(src);
    fclose(dst);
    babel_free(fp); babel_free(tp);
    return babel_nothing();
}

/* Path builtins */

BabelValue *babel_home_dir(void) {
    const char *h = getenv("HOME");
    return babel_string(h ? h : "/");
}

BabelValue *babel_current_dir(void) {
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) return babel_string(buf);
    return babel_string(".");
}

BabelValue *babel_temp_dir(void) {
    const char *t = getenv("TMPDIR");
    if (!t) t = getenv("TMP");
    if (!t) t = "/tmp";
    int len = strlen(t);
    if (len > 1 && t[len - 1] == '/') {
        char *buf = babel_alloc(len);
        memcpy(buf, t, len - 1);
        buf[len - 1] = '\0';
        BabelValue *v = babel_string(buf);
        babel_free(buf);
        return v;
    }
    return babel_string(t);
}

static BabelValue *home_subdir(const char *sub) {
    const char *h = getenv("HOME");
    if (!h) return babel_string(sub);
    int hlen = strlen(h);
    int slen = strlen(sub);
    char *buf = babel_alloc(hlen + slen + 2);
    memcpy(buf, h, hlen);
    buf[hlen] = '/';
    memcpy(buf + hlen + 1, sub, slen);
    buf[hlen + 1 + slen] = '\0';
    BabelValue *v = babel_string(buf);
    babel_free(buf);
    return v;
}

BabelValue *babel_desktop_dir(void)   { return home_subdir("Desktop"); }
BabelValue *babel_downloads_dir(void) { return home_subdir("Downloads"); }
BabelValue *babel_documents_dir(void) { return home_subdir("Documents"); }

BabelValue *babel_expand_path(BabelValue *path) {
    if (path->type != VAL_STRING) return babel_retain(path);
    char *e = expand_path_str(path->as.string);
    BabelValue *result = babel_string(e);
    babel_free(e);
    return result;
}

BabelValue *babel_file_lines(BabelValue *path) {
    BabelValue *content = babel_file_read(path);
    BabelValue *result = babel_list_new();
    if (content->type != VAL_STRING) {
        babel_release(content);
        return result;
    }
    const char *s = content->as.string;
    const char *line_start = s;
    while (*s) {
        if (*s == '\n') {
            int len = s - line_start;
            char *buf = babel_alloc(len + 1);
            memcpy(buf, line_start, len);
            buf[len] = '\0';
            BabelValue *line_val = babel_string(buf);
            babel_list_append(result, line_val);
            babel_release(line_val);
            babel_free(buf);
            line_start = s + 1;
        }
        s++;
    }
    /* Last line without trailing newline */
    if (s > line_start) {
        int len = s - line_start;
        char *buf = babel_alloc(len + 1);
        memcpy(buf, line_start, len);
        buf[len] = '\0';
        BabelValue *line_val = babel_string(buf);
        babel_list_append(result, line_val);
        babel_release(line_val);
        babel_free(buf);
    }
    babel_release(content);
    return result;
}
