#include "esu_template.h"
#include "esu_map.h"
#include "esu_list.h"
#include "esu_mem.h"
#include "esu_error.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

/* ===== Growable string buffer ===== */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} Buf;

static void buf_init(Buf *b) {
    b->cap = 256;
    b->len = 0;
    b->buf = esu_alloc(b->cap);
    b->buf[0] = '\0';
}

static void buf_grow(Buf *b, size_t need) {
    if (b->len + need + 1 <= b->cap) return;
    while (b->len + need + 1 > b->cap) b->cap *= 2;
    b->buf = esu_realloc(b->buf, b->cap);
}

static void buf_append(Buf *b, const char *s, size_t n) {
    buf_grow(b, n);
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void buf_append_str(Buf *b, const char *s) {
    buf_append(b, s, strlen(s));
}

/* ===== HTML escape ===== */

char *esu_html_escape(const char *s) {
    Buf b; buf_init(&b);
    for (; *s; s++) {
        switch (*s) {
            case '<':  buf_append_str(&b, "&lt;"); break;
            case '>':  buf_append_str(&b, "&gt;"); break;
            case '&':  buf_append_str(&b, "&amp;"); break;
            case '"':  buf_append_str(&b, "&quot;"); break;
            case '\'': buf_append_str(&b, "&#39;"); break;
            default:   buf_append(&b, s, 1); break;
        }
    }
    return b.buf;
}

/* ===== Expression evaluator (dotted + indexed path) =====
 *
 * Supports: NAME, NAME.field, NAME[INDEX], NAME[KEY], where index/key is
 * either an integer literal, a quoted string literal, or another NAME
 * (resolved in the same context).
 *
 * No arithmetic or function calls — keep the engine pure data access.
 * Returns a borrowed reference to a value owned by the context graph, OR
 * a freshly-retained value the caller must release. `*owned` is set to
 * indicate which.
 */

/* Forward decl. */
static EsuValue *eval_path(const char *expr_start, const char *expr_end,
                           EsuValue *ctx, int *owned);

/* Skip leading ASCII whitespace. */
static const char *skip_ws(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) p++;
    return p;
}

/* Read an identifier [A-Za-z_][A-Za-z0-9_-]* into out (NUL-terminated). */
static const char *read_ident(const char *p, const char *end, char *out, size_t cap) {
    size_t i = 0;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_' || *p == '-') &&
           i + 1 < cap) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return p;
}

/* Resolve a key/index segment to an EsuValue (caller releases if *owned). */
static EsuValue *resolve_index(const char *start, const char *end,
                               EsuValue *ctx, int *owned) {
    *owned = 1;
    start = skip_ws(start, end);
    if (start >= end) return esu_nothing();

    /* Integer literal */
    if (isdigit((unsigned char)*start) || *start == '-') {
        char num[32]; size_t i = 0;
        while (start < end && (isdigit((unsigned char)*start) || *start == '-') && i < 31) {
            num[i++] = *start++;
        }
        num[i] = '\0';
        return esu_number((double)atoi(num));
    }

    /* Quoted string literal */
    if (*start == '"' || *start == '\'') {
        char q = *start++;
        Buf b; buf_init(&b);
        while (start < end && *start != q) {
            buf_append(&b, start, 1);
            start++;
        }
        EsuValue *v = esu_string(b.buf);
        esu_free(b.buf);
        return v;
    }

    /* Identifier — look up in ctx */
    char name[128];
    read_ident(start, end, name, sizeof(name));
    int sub_owned = 0;
    /* eval_path handles single-name lookup too. */
    return eval_path(name, name + strlen(name), ctx, &sub_owned);
}

/* Step through a path: `head` is the EsuValue we've resolved so far, `p..end`
 * is the remaining path text (either `.field` or `[expr]`). */
static EsuValue *step_path(EsuValue *head, int head_owned,
                           const char *p, const char *end,
                           EsuValue *ctx, int *owned) {
    EsuValue *cur = head;
    int cur_owned = head_owned;
    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end) break;
        if (*p == '.') {
            p++;
            char field[128];
            p = read_ident(p, end, field, sizeof(field));
            if (!cur || cur->type != VAL_MAP) {
                if (cur_owned && cur) esu_release(cur);
                cur = esu_nothing();
                cur_owned = 1;
                continue;
            }
            EsuValue *key = esu_string(field);
            EsuValue *next = esu_map_get(cur, key);
            esu_release(key);
            if (cur_owned) esu_release(cur);
            cur = next ? next : esu_nothing();
            cur_owned = 1;
        } else if (*p == '[') {
            p++;
            const char *close = p;
            int depth = 1;
            while (close < end && depth > 0) {
                if (*close == '[') depth++;
                else if (*close == ']') depth--;
                if (depth > 0) close++;
            }
            if (close >= end) break; /* malformed */
            int idx_owned = 0;
            EsuValue *idx = resolve_index(p, close, ctx, &idx_owned);
            EsuValue *next = NULL;
            if (cur && cur->type == VAL_LIST && idx && idx->type == VAL_NUMBER) {
                int i = (int)idx->as.number;
                if (i >= 0 && i < cur->as.list->count) {
                    next = esu_retain(cur->as.list->items[i]);
                }
            } else if (cur && cur->type == VAL_MAP && idx) {
                /* Cast non-string keys to string. */
                if (idx->type == VAL_STRING) {
                    next = esu_map_get(cur, idx);
                } else {
                    char *s = esu_to_cstring(idx);
                    EsuValue *k = esu_string(s);
                    next = esu_map_get(cur, k);
                    esu_release(k);
                    esu_free(s);
                }
            }
            if (idx_owned) esu_release(idx);
            if (cur_owned) esu_release(cur);
            cur = next ? next : esu_nothing();
            cur_owned = 1;
            p = close + 1;
        } else {
            break;
        }
    }
    *owned = cur_owned;
    return cur;
}

static EsuValue *eval_path(const char *expr_start, const char *expr_end,
                           EsuValue *ctx, int *owned) {
    const char *p = skip_ws(expr_start, expr_end);
    if (p >= expr_end) { *owned = 1; return esu_nothing(); }

    /* Quoted string literal at the start (rare but legal). */
    if (*p == '"' || *p == '\'') {
        return resolve_index(p, expr_end, ctx, owned);
    }
    /* Numeric literal at the start. */
    if (isdigit((unsigned char)*p) || *p == '-') {
        const char *p2 = p;
        int looks_numeric = 1;
        while (p2 < expr_end && !isspace((unsigned char)*p2) &&
               *p2 != '.' && *p2 != '[' && *p2 != '|') {
            if (!isdigit((unsigned char)*p2) && *p2 != '-' && *p2 != '+') {
                looks_numeric = 0; break;
            }
            p2++;
        }
        if (looks_numeric) return resolve_index(p, p2, ctx, owned);
    }

    /* Identifier lookup */
    char name[128];
    const char *after = read_ident(p, expr_end, name, sizeof(name));
    if (name[0] == '\0') { *owned = 1; return esu_nothing(); }

    EsuValue *root;
    int root_owned;
    if (ctx && ctx->type == VAL_MAP) {
        EsuValue *key = esu_string(name);
        root = esu_map_get(ctx, key);
        esu_release(key);
        root_owned = 1;
        if (!root) { root = esu_nothing(); }
    } else {
        root = esu_nothing(); root_owned = 1;
    }
    return step_path(root, root_owned, after, expr_end, ctx, owned);
}

/* Truthiness for `{% if %}`. */
static int value_truthy(EsuValue *v) {
    if (!v) return 0;
    switch (v->type) {
        case VAL_NOTHING: return 0;
        case VAL_BOOL:    return v->as.boolean != 0;
        case VAL_NUMBER:  return v->as.number != 0.0;
        case VAL_STRING:  return v->as.string && v->as.string[0] != '\0';
        case VAL_LIST:    return v->as.list && v->as.list->count > 0;
        case VAL_MAP:     return v->as.map && v->as.map->count > 0;
        default:          return 1;
    }
}

/* ===== File loading + cache ===== */

typedef struct CacheEntry {
    char *path;
    char *content;
    time_t mtime;
    struct CacheEntry *next;
} CacheEntry;

static CacheEntry *cache_head = NULL;

static char *load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = esu_alloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static const char *get_cached(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return NULL;
    for (CacheEntry *e = cache_head; e; e = e->next) {
        if (strcmp(e->path, path) == 0) {
            if (e->mtime == st.st_mtime) return e->content;
            /* stale — fall through to reload */
            esu_free(e->content);
            e->content = load_file(path);
            e->mtime = st.st_mtime;
            return e->content;
        }
    }
    CacheEntry *e = esu_alloc(sizeof(CacheEntry));
    e->path = esu_strdup(path);
    e->content = load_file(path);
    e->mtime = st.st_mtime;
    e->next = cache_head;
    cache_head = e;
    return e->content;
}

/* ===== Renderer ===== */

/* Forward declaration; render_chunk operates on src[start..end) and writes
 * into `out`. Context is `ctx`; `loop_var` and `loop_val`, if non-NULL,
 * provide a single inline binding for the current `{% for each %}` body
 * (we layer it over ctx so a loop variable shadows context keys). */
static void render_chunk(Buf *out, const char *src, size_t end,
                         size_t *posp, EsuValue *ctx);

/* Find the next directive opener (`{{`, `{%`, `{#`) starting at `*p`.
 * Writes to *p the position of `{`; sets *kind to:
 *   1 = `{{`    (interpolation, closed by `}}`)
 *   2 = `{%`    (control, closed by `%}`)
 *   3 = `{#`    (comment, closed by `#}`)
 * Returns 1 on found, 0 on end-of-input.
 *
 * Single `{` (not followed by `{`, `%`, or `#`) is a literal. */
static int find_directive(const char *src, size_t end, size_t *p, int *kind) {
    while (*p < end) {
        if (src[*p] == '{' && *p + 1 < end) {
            char next = src[*p + 1];
            if (next == '{') { *kind = 1; return 1; }
            if (next == '%') { *kind = 2; return 1; }
            if (next == '#') { *kind = 3; return 1; }
        }
        (*p)++;
    }
    return 0;
}

/* Find the closing sequence for a directive that opened at `start` with
 * `kind` (as from find_directive). Returns the index of the first char AFTER
 * the directive (past `}}`, `%}`, or `#}`). The content positions are written
 * to *body_start, *body_end. */
static size_t find_close(const char *src, size_t end, size_t start, int kind,
                         size_t *body_start, size_t *body_end) {
    const char *terminator;
    switch (kind) {
        case 1:  terminator = "}}"; break;
        case 2:  terminator = "%}"; break;
        default: terminator = "#}"; break;
    }
    *body_start = start + 2;
    size_t p = start + 2;
    while (p + 1 < end) {
        if (src[p] == terminator[0] && src[p + 1] == terminator[1]) {
            *body_end = p;
            return p + 2;
        }
        p++;
    }
    *body_end = end;
    return end;
}

/* Format an EsuValue as a string for interpolation. Caller frees. */
static char *value_to_display_string(EsuValue *v) {
    if (!v || v->type == VAL_NOTHING) return esu_strdup("");
    if (v->type == VAL_STRING) return esu_strdup(v->as.string);
    return esu_to_cstring(v);
}

/* Parse the `{...}` interior: an expression possibly followed by `| filter`.
 * Currently the only filter is `raw` (opt out of escaping). Writes a string
 * to `out`. */
static void interpolate(Buf *out, const char *body, size_t blen, EsuValue *ctx) {
    /* Split on `|` */
    const char *p = body;
    const char *end = body + blen;
    const char *bar = NULL;
    for (const char *q = p; q < end; q++) {
        if (*q == '|') { bar = q; break; }
    }
    const char *expr_end = bar ? bar : end;
    int owned = 0;
    EsuValue *v = eval_path(p, expr_end, ctx, &owned);

    int raw = 0;
    if (bar) {
        const char *f = skip_ws(bar + 1, end);
        const char *fend = f;
        while (fend < end && !isspace((unsigned char)*fend)) fend++;
        if ((size_t)(fend - f) == 3 && strncmp(f, "raw", 3) == 0) raw = 1;
        /* Unknown filters are silently ignored — escape by default. */
    }

    char *display = value_to_display_string(v);
    if (raw) {
        buf_append_str(out, display);
    } else {
        char *esc = esu_html_escape(display);
        buf_append_str(out, esc);
        esu_free(esc);
    }
    esu_free(display);
    if (owned && v) esu_release(v);
}

/* Find the matching `{% end %}` (or `{% else %}` at the same nesting level)
 * starting at `src + start`. Returns the offset of the matching directive's
 * opening `{`. *kind_out is 1 for `else`, 2 for `end`. */
static size_t find_block_terminator(const char *src, size_t end, size_t start,
                                    int *kind_out) {
    int depth = 0;
    size_t p = start;
    while (p < end) {
        int k;
        size_t pp = p;
        if (!find_directive(src, end, &pp, &k)) {
            *kind_out = 2;
            return end;
        }
        if (k != 2) {
            /* Skip past whichever directive we found. */
            size_t bs, be;
            p = find_close(src, end, pp, k, &bs, &be);
            continue;
        }
        size_t bs, be;
        size_t after = find_close(src, end, pp, k, &bs, &be);
        /* Inspect the directive keyword. */
        const char *cmd = src + bs;
        const char *cend = cmd;
        while (cend < src + be && isspace((unsigned char)*cend)) cend++;
        const char *kw = cend;
        while (cend < src + be && !isspace((unsigned char)*cend)) cend++;
        size_t klen = (size_t)(cend - kw);

        if (klen == 2 && strncmp(kw, "if", 2) == 0) depth++;
        else if (klen == 8 && strncmp(kw, "for", 3) == 0) depth++;
        else if (klen == 3 && strncmp(kw, "for", 3) == 0) depth++;
        else if (klen == 4 && strncmp(kw, "else", 4) == 0 && depth == 0) {
            *kind_out = 1;
            return pp;
        }
        else if (klen == 3 && strncmp(kw, "end", 3) == 0) {
            if (depth == 0) { *kind_out = 2; return pp; }
            depth--;
        }
        p = after;
    }
    *kind_out = 2;
    return end;
}

/* Render src[start..end) into `out`. Mutates *posp = end on return. */
static void render_chunk(Buf *out, const char *src, size_t end,
                         size_t *posp, EsuValue *ctx) {
    size_t p = *posp;
    while (p < end) {
        size_t hit = p;
        int kind;
        int found = find_directive(src, end, &hit, &kind);
        if (!found) {
            /* Maybe stopped on an escaped `{{` — handle that and continue. */
            if (hit < end && src[hit] == '{' && hit + 1 < end && src[hit + 1] == '{') {
                buf_append(out, src + p, hit - p);
                buf_append(out, "{", 1);
                p = hit + 2;
                continue;
            }
            /* No more directives → flush rest */
            buf_append(out, src + p, end - p);
            p = end;
            break;
        }
        /* Flush literal text before the directive. */
        if (hit > p) buf_append(out, src + p, hit - p);

        size_t bs, be;
        size_t after = find_close(src, end, hit, kind, &bs, &be);

        if (kind == 3) {
            /* Comment — discard. */
            p = after;
            continue;
        }
        if (kind == 1) {
            /* Interpolation. */
            interpolate(out, src + bs, be - bs, ctx);
            p = after;
            continue;
        }
        /* kind == 2: control block. Identify the keyword. */
        const char *cmd = src + bs;
        const char *cend = src + be;
        const char *kw = skip_ws(cmd, cend);
        const char *kw_end = kw;
        while (kw_end < cend && !isspace((unsigned char)*kw_end)) kw_end++;
        size_t klen = (size_t)(kw_end - kw);

        if (klen == 2 && strncmp(kw, "if", 2) == 0) {
            /* {% if EXPR %} */
            const char *expr_start = skip_ws(kw_end, cend);
            int owned = 0;
            EsuValue *cond = eval_path(expr_start, cend, ctx, &owned);
            int t = value_truthy(cond);
            if (owned && cond) esu_release(cond);

            int term_kind;
            size_t else_or_end = find_block_terminator(src, end, after, &term_kind);
            size_t branch_start = after;
            size_t branch_end = else_or_end;
            size_t cont;
            if (term_kind == 1) {
                /* Found `{% else %}` — advance past it, then find the `{% end %}`. */
                size_t ebs, ebe;
                size_t after_else = find_close(src, end, else_or_end, 2, &ebs, &ebe);
                int end_kind;
                size_t end_pos = find_block_terminator(src, end, after_else, &end_kind);
                if (t) {
                    branch_end = else_or_end;
                    /* render branch_start..branch_end */
                } else {
                    branch_start = after_else;
                    branch_end = end_pos;
                }
                size_t ebs2, ebe2;
                cont = find_close(src, end, end_pos, 2, &ebs2, &ebe2);
            } else {
                /* End only (no else). If condition false, branch is empty. */
                if (!t) branch_start = branch_end;
                size_t ebs2, ebe2;
                cont = find_close(src, end, else_or_end, 2, &ebs2, &ebe2);
            }
            /* Render the chosen branch. */
            size_t inner_pos = branch_start;
            render_chunk(out, src, branch_end, &inner_pos, ctx);
            p = cont;
            continue;
        }

        if (klen == 3 && strncmp(kw, "for", 3) == 0) {
            /* {% for each ITEM in EXPR %} … {% end %} */
            const char *r = skip_ws(kw_end, cend);
            /* "each" or "every" optional */
            if ((cend - r) >= 4 && strncmp(r, "each", 4) == 0) r = skip_ws(r + 4, cend);
            else if ((cend - r) >= 5 && strncmp(r, "every", 5) == 0) r = skip_ws(r + 5, cend);
            char var_name[128];
            r = read_ident(r, cend, var_name, sizeof(var_name));
            r = skip_ws(r, cend);
            if ((cend - r) >= 2 && strncmp(r, "in", 2) == 0) r = skip_ws(r + 2, cend);
            const char *expr_start = r;

            int owned = 0;
            EsuValue *list = eval_path(expr_start, cend, ctx, &owned);

            int end_kind;
            size_t end_pos = find_block_terminator(src, end, after, &end_kind);
            size_t body_start = after;
            size_t body_end = end_pos;
            size_t ebs2, ebe2;
            size_t cont = find_close(src, end, end_pos, 2, &ebs2, &ebe2);

            if (list && list->type == VAL_LIST) {
                /* Layer the loop var over the context: shallow-copy ctx into a
                 * fresh map with var_name bound to each item. */
                for (int i = 0; i < list->as.list->count; i++) {
                    EsuValue *inner = esu_map_new();
                    if (ctx && ctx->type == VAL_MAP) {
                        EsuMap *m = ctx->as.map;
                        for (int j = 0; j < m->count; j++) {
                            EsuValue *k = esu_string(m->entries[j].key);
                            esu_map_set(inner, k, m->entries[j].value);
                            esu_release(k);
                        }
                    }
                    EsuValue *k = esu_string(var_name);
                    esu_map_set(inner, k, list->as.list->items[i]);
                    esu_release(k);
                    size_t inner_pos = body_start;
                    render_chunk(out, src, body_end, &inner_pos, inner);
                    esu_release(inner);
                }
            }
            if (owned && list) esu_release(list);
            p = cont;
            continue;
        }

        if (klen == 7 && strncmp(kw, "include", 7) == 0) {
            /* {% include "PATH" %} */
            const char *r = skip_ws(kw_end, cend);
            if (r < cend && (*r == '"' || *r == '\'')) {
                char q = *r++;
                const char *path_start = r;
                while (r < cend && *r != q) r++;
                size_t plen = (size_t)(r - path_start);
                char path[1024];
                if (plen >= sizeof(path)) plen = sizeof(path) - 1;
                memcpy(path, path_start, plen);
                path[plen] = '\0';
                const char *content = get_cached(path);
                if (content) {
                    size_t inner_pos = 0;
                    render_chunk(out, content, strlen(content), &inner_pos, ctx);
                }
            }
            p = after;
            continue;
        }

        /* Unknown control — skip silently. */
        p = after;
    }
    *posp = end;
}

/* ===== Public API ===== */

EsuValue *esu_render_string(EsuValue *template_str, EsuValue *context) {
    if (!template_str || template_str->type != VAL_STRING) {
        esu_raise_msg("template must be a string");
        return esu_nothing();
    }
    const char *src = template_str->as.string;
    size_t len = strlen(src);
    Buf out; buf_init(&out);
    size_t pos = 0;
    render_chunk(&out, src, len, &pos, context);
    EsuValue *v = esu_string(out.buf);
    esu_free(out.buf);
    return v;
}

EsuValue *esu_render_file(EsuValue *path, EsuValue *context) {
    if (!path || path->type != VAL_STRING) {
        esu_raise_msg("template path must be a string");
        return esu_nothing();
    }
    const char *content = get_cached(path->as.string);
    if (!content) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot read template file: %s", path->as.string);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    Buf out; buf_init(&out);
    size_t pos = 0;
    render_chunk(&out, content, strlen(content), &pos, context);
    EsuValue *v = esu_string(out.buf);
    esu_free(out.buf);
    return v;
}

/* Heuristic: if the arg contains '<' or '{', treat as inline template;
 * otherwise treat as a file path. */
EsuValue *esu_render(EsuValue *template_or_path, EsuValue *context) {
    if (!template_or_path || template_or_path->type != VAL_STRING) {
        esu_raise_msg("render argument must be a string (template or path)");
        return esu_nothing();
    }
    const char *s = template_or_path->as.string;
    if (strchr(s, '<') || strchr(s, '{')) {
        return esu_render_string(template_or_path, context);
    }
    return esu_render_file(template_or_path, context);
}
