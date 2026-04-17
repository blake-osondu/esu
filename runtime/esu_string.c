#include "esu_string.h"
#include "esu_mem.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

EsuValue *esu_length(EsuValue *val) {
    if (val->type == VAL_STRING) {
        return esu_number((double)strlen(val->as.string));
    }
    if (val->type == VAL_LIST) {
        return esu_number((double)val->as.list->count);
    }
    if (val->type == VAL_MAP) {
        return esu_number((double)val->as.map->count);
    }
    return esu_number(0);
}

EsuValue *esu_substring(EsuValue *str, EsuValue *start, EsuValue *end) {
    if (str->type != VAL_STRING) return esu_string("");
    const char *s = str->as.string;
    int slen = strlen(s);
    int a = (int)start->as.number;
    int b = (int)end->as.number;
    if (a < 0) a = 0;
    if (b > slen) b = slen;
    if (a >= b) return esu_string("");
    int rlen = b - a;
    char *buf = esu_alloc(rlen + 1);
    memcpy(buf, s + a, rlen);
    buf[rlen] = '\0';
    EsuValue *result = esu_string(buf);
    esu_free(buf);
    return result;
}

EsuValue *esu_contains(EsuValue *str, EsuValue *search) {
    if (str->type == VAL_STRING && search->type == VAL_STRING) {
        return esu_bool(strstr(str->as.string, search->as.string) != NULL);
    }
    if (str->type == VAL_LIST) {
        /* Delegate to list_contains */
        EsuList *list = str->as.list;
        for (int i = 0; i < list->count; i++) {
            EsuValue *a = list->items[i];
            if (a->type == search->type) {
                if (a->type == VAL_NUMBER && a->as.number == search->as.number) return esu_bool(1);
                if (a->type == VAL_STRING && strcmp(a->as.string, search->as.string) == 0) return esu_bool(1);
                if (a->type == VAL_BOOL && a->as.boolean == search->as.boolean) return esu_bool(1);
            }
        }
        return esu_bool(0);
    }
    return esu_bool(0);
}

EsuValue *esu_starts_with(EsuValue *str, EsuValue *prefix) {
    if (str->type != VAL_STRING || prefix->type != VAL_STRING) return esu_bool(0);
    return esu_bool(strncmp(str->as.string, prefix->as.string, strlen(prefix->as.string)) == 0);
}

EsuValue *esu_ends_with(EsuValue *str, EsuValue *suffix) {
    if (str->type != VAL_STRING || suffix->type != VAL_STRING) return esu_bool(0);
    int slen = strlen(str->as.string);
    int suflen = strlen(suffix->as.string);
    if (suflen > slen) return esu_bool(0);
    return esu_bool(strcmp(str->as.string + slen - suflen, suffix->as.string) == 0);
}

EsuValue *esu_to_upper(EsuValue *str) {
    if (str->type != VAL_STRING) return esu_string("");
    char *buf = esu_strdup(str->as.string);
    for (int i = 0; buf[i]; i++) buf[i] = toupper((unsigned char)buf[i]);
    EsuValue *result = esu_string(buf);
    esu_free(buf);
    return result;
}

EsuValue *esu_to_lower(EsuValue *str) {
    if (str->type != VAL_STRING) return esu_string("");
    char *buf = esu_strdup(str->as.string);
    for (int i = 0; buf[i]; i++) buf[i] = tolower((unsigned char)buf[i]);
    EsuValue *result = esu_string(buf);
    esu_free(buf);
    return result;
}

EsuValue *esu_trim(EsuValue *str) {
    if (str->type != VAL_STRING) return esu_string("");
    const char *s = str->as.string;
    while (*s && isspace((unsigned char)*s)) s++;
    int len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char *buf = esu_alloc(len + 1);
    memcpy(buf, s, len);
    buf[len] = '\0';
    EsuValue *result = esu_string(buf);
    esu_free(buf);
    return result;
}

EsuValue *esu_replace(EsuValue *str, EsuValue *old_s, EsuValue *new_s) {
    if (str->type != VAL_STRING || old_s->type != VAL_STRING || new_s->type != VAL_STRING)
        return esu_string("");
    const char *src = str->as.string;
    const char *find = old_s->as.string;
    const char *repl = new_s->as.string;
    int find_len = strlen(find);
    int repl_len = strlen(repl);
    if (find_len == 0) return esu_string(src);

    /* Count occurrences */
    int count = 0;
    const char *p = src;
    while ((p = strstr(p, find)) != NULL) { count++; p += find_len; }

    int result_len = strlen(src) + count * (repl_len - find_len);
    char *buf = esu_alloc(result_len + 1);
    char *out = buf;
    p = src;
    const char *found;
    while ((found = strstr(p, find)) != NULL) {
        int chunk = found - p;
        memcpy(out, p, chunk);
        out += chunk;
        memcpy(out, repl, repl_len);
        out += repl_len;
        p = found + find_len;
    }
    strcpy(out, p);
    EsuValue *result = esu_string(buf);
    esu_free(buf);
    return result;
}

EsuValue *esu_split(EsuValue *str, EsuValue *delim) {
    /* Returns a string representation for now — lists come in Phase 6 */
    (void)delim;
    if (str->type != VAL_STRING) return esu_string("");
    return esu_string(str->as.string);
}

EsuValue *esu_char_at(EsuValue *str, EsuValue *index) {
    if (str->type != VAL_STRING) return esu_string("");
    int i = (int)index->as.number;
    int len = strlen(str->as.string);
    if (i < 0 || i >= len) return esu_string("");
    char buf[2] = { str->as.string[i], '\0' };
    return esu_string(buf);
}

EsuValue *esu_last_char(EsuValue *str) {
    if (str->type != VAL_STRING) return esu_string("");
    int len = strlen(str->as.string);
    if (len == 0) return esu_string("");
    char buf[2] = { str->as.string[len - 1], '\0' };
    return esu_string(buf);
}

EsuValue *esu_input(EsuValue *prompt) {
    if (prompt->type == VAL_STRING) {
        printf("%s", prompt->as.string);
        fflush(stdout);
    }
    return esu_input_no_prompt();
}

EsuValue *esu_input_no_prompt(void) {
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin)) {
        /* Strip trailing newline */
        int len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        return esu_string(buf);
    }
    return esu_string("");
}
