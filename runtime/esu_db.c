#include "esu_db.h"
#include "esu_map.h"
#include "esu_list.h"
#include "esu_mem.h"
#include "esu_error.h"
#include "sqlite3.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ===== Internal helpers ===== */

/* Get the sqlite3* hidden behind a db handle map. NULL if invalid. */
static sqlite3 *db_handle(EsuValue *db) {
    if (!db || db->type != VAL_MAP) return NULL;
    EsuValue *p = esu_map_get(db, esu_string("__ctx"));
    if (!p || p->type != VAL_NUMBER) {
        if (p) esu_release(p);
        return NULL;
    }
    sqlite3 *h = (sqlite3 *)(uintptr_t)p->as.number;
    esu_release(p);
    return h;
}

/* Bind a single EsuValue to parameter index `i` (1-based) on a prepared stmt.
 * Returns SQLITE_OK or an error code. */
static int bind_one(sqlite3_stmt *stmt, int i, EsuValue *v) {
    if (!v || v->type == VAL_NOTHING) return sqlite3_bind_null(stmt, i);
    switch (v->type) {
        case VAL_NUMBER:
            if (v->as.number == (double)(long long)v->as.number) {
                return sqlite3_bind_int64(stmt, i, (sqlite3_int64)v->as.number);
            }
            return sqlite3_bind_double(stmt, i, v->as.number);
        case VAL_BOOL:
            return sqlite3_bind_int(stmt, i, v->as.boolean ? 1 : 0);
        case VAL_STRING:
            return sqlite3_bind_text(stmt, i, v->as.string, -1, SQLITE_TRANSIENT);
        default: {
            /* Fall back to to_cstring for unsupported types (lists, maps). */
            char *s = esu_to_cstring(v);
            int rc = sqlite3_bind_text(stmt, i, s, -1, SQLITE_TRANSIENT);
            esu_free(s);
            return rc;
        }
    }
}

/* Bind a list of EsuValues to ?-placeholders 1..N on a prepared statement. */
static int bind_params(sqlite3_stmt *stmt, EsuValue *params) {
    if (!params || params->type != VAL_LIST) return SQLITE_OK;
    int n = params->as.list->count;
    int param_n = sqlite3_bind_parameter_count(stmt);
    int bind_n = n < param_n ? n : param_n;
    for (int i = 0; i < bind_n; i++) {
        int rc = bind_one(stmt, i + 1, params->as.list->items[i]);
        if (rc != SQLITE_OK) return rc;
    }
    return SQLITE_OK;
}

/* Convert a single result-set column to an EsuValue. */
static EsuValue *column_to_value(sqlite3_stmt *stmt, int col) {
    switch (sqlite3_column_type(stmt, col)) {
        case SQLITE_INTEGER: return esu_number((double)sqlite3_column_int64(stmt, col));
        case SQLITE_FLOAT:   return esu_number(sqlite3_column_double(stmt, col));
        case SQLITE_NULL:    return esu_nothing();
        case SQLITE_TEXT:
        case SQLITE_BLOB:
        default: {
            const unsigned char *txt = sqlite3_column_text(stmt, col);
            return esu_string(txt ? (const char *)txt : "");
        }
    }
}

/* Raise a SQLite error with a useful message. */
static void raise_sqlite_error(sqlite3 *h, const char *while_doing) {
    char msg[512];
    snprintf(msg, sizeof(msg), "%s: %s", while_doing,
             h ? sqlite3_errmsg(h) : "no database handle");
    esu_raise_msg(msg);
}

/* ===== Public API ===== */

EsuValue *esu_db_open(EsuValue *path) {
    if (!path || path->type != VAL_STRING) {
        esu_raise_msg("database path must be a string");
        return esu_nothing();
    }
    sqlite3 *h = NULL;
    int rc = sqlite3_open(path->as.string, &h);
    if (rc != SQLITE_OK) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot open database '%s': %s",
                 path->as.string, h ? sqlite3_errmsg(h) : sqlite3_errstr(rc));
        if (h) sqlite3_close(h);
        esu_raise_msg(msg);
        return esu_nothing();
    }

    EsuValue *db = esu_map_new();
    esu_map_set(db, esu_string("path"), path);
    esu_map_set(db, esu_string("__ctx"),
                esu_number((double)(uintptr_t)h));
    return db;
}

void esu_db_close(EsuValue *db) {
    sqlite3 *h = db_handle(db);
    if (!h) return;
    sqlite3_close(h);
    /* Zero the hidden pointer so a second close is a no-op. */
    EsuValue *zero = esu_number(0);
    esu_map_set(db, esu_string("__ctx"), zero);
    esu_release(zero);
}

EsuValue *esu_db_exec(EsuValue *db, EsuValue *sql) {
    sqlite3 *h = db_handle(db);
    if (!h) { esu_raise_msg("invalid database handle"); return esu_nothing(); }
    if (!sql || sql->type != VAL_STRING) {
        esu_raise_msg("SQL must be a string");
        return esu_nothing();
    }
    char *errmsg = NULL;
    int rc = sqlite3_exec(h, sql->as.string, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        char msg[512];
        snprintf(msg, sizeof(msg), "SQL error: %s", errmsg ? errmsg : "unknown");
        sqlite3_free(errmsg);
        esu_raise_msg(msg);
        return esu_nothing();
    }
    EsuValue *result = esu_map_new();
    EsuValue *changes = esu_number((double)sqlite3_changes(h));
    EsuValue *last_id = esu_number((double)sqlite3_last_insert_rowid(h));
    esu_map_set(result, esu_string("changes"), changes);
    esu_map_set(result, esu_string("last_id"), last_id);
    esu_release(changes);
    esu_release(last_id);
    return result;
}

EsuValue *esu_db_run(EsuValue *db, EsuValue *sql, EsuValue *params) {
    sqlite3 *h = db_handle(db);
    if (!h) { esu_raise_msg("invalid database handle"); return esu_nothing(); }
    if (!sql || sql->type != VAL_STRING) {
        esu_raise_msg("SQL must be a string");
        return esu_nothing();
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(h, sql->as.string, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        raise_sqlite_error(h, "prepare");
        return esu_nothing();
    }
    if (bind_params(stmt, params) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        raise_sqlite_error(h, "bind params");
        return esu_nothing();
    }

    rc = sqlite3_step(stmt);
    /* For non-SELECT, expect SQLITE_DONE. SELECT in run() is allowed but
     * only the first row's side-effects matter; users should use query. */
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        raise_sqlite_error(h, "execute");
        return esu_nothing();
    }
    sqlite3_finalize(stmt);

    EsuValue *result = esu_map_new();
    EsuValue *changes = esu_number((double)sqlite3_changes(h));
    EsuValue *last_id = esu_number((double)sqlite3_last_insert_rowid(h));
    esu_map_set(result, esu_string("changes"), changes);
    esu_map_set(result, esu_string("last_id"), last_id);
    esu_release(changes);
    esu_release(last_id);
    return result;
}

/* Internal: run a prepared SELECT and accumulate rows into a list. */
static EsuValue *step_to_rows(sqlite3_stmt *stmt) {
    EsuValue *rows = esu_list_new();
    int ncols = sqlite3_column_count(stmt);
    for (;;) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            esu_release(rows);
            return NULL;
        }
        EsuValue *row = esu_map_new();
        for (int i = 0; i < ncols; i++) {
            const char *name = sqlite3_column_name(stmt, i);
            EsuValue *key = esu_string(name ? name : "?");
            EsuValue *val = column_to_value(stmt, i);
            esu_map_set(row, key, val);
            esu_release(key);
            esu_release(val);
        }
        esu_list_append(rows, row);
        esu_release(row);
    }
    return rows;
}

EsuValue *esu_db_query(EsuValue *db, EsuValue *sql) {
    sqlite3 *h = db_handle(db);
    if (!h) { esu_raise_msg("invalid database handle"); return esu_nothing(); }
    if (!sql || sql->type != VAL_STRING) {
        esu_raise_msg("SQL must be a string");
        return esu_nothing();
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(h, sql->as.string, -1, &stmt, NULL) != SQLITE_OK) {
        raise_sqlite_error(h, "prepare");
        return esu_nothing();
    }
    EsuValue *rows = step_to_rows(stmt);
    sqlite3_finalize(stmt);
    if (!rows) {
        raise_sqlite_error(h, "query");
        return esu_nothing();
    }
    return rows;
}

EsuValue *esu_db_query_with(EsuValue *db, EsuValue *sql, EsuValue *params) {
    sqlite3 *h = db_handle(db);
    if (!h) { esu_raise_msg("invalid database handle"); return esu_nothing(); }
    if (!sql || sql->type != VAL_STRING) {
        esu_raise_msg("SQL must be a string");
        return esu_nothing();
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(h, sql->as.string, -1, &stmt, NULL) != SQLITE_OK) {
        raise_sqlite_error(h, "prepare");
        return esu_nothing();
    }
    if (bind_params(stmt, params) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        raise_sqlite_error(h, "bind params");
        return esu_nothing();
    }
    EsuValue *rows = step_to_rows(stmt);
    sqlite3_finalize(stmt);
    if (!rows) {
        raise_sqlite_error(h, "query");
        return esu_nothing();
    }
    return rows;
}

void esu_db_begin(EsuValue *db) {
    EsuValue *sql = esu_string("BEGIN");
    EsuValue *r = esu_db_exec(db, sql);
    esu_release(sql);
    if (r) esu_release(r);
}

void esu_db_commit(EsuValue *db) {
    EsuValue *sql = esu_string("COMMIT");
    EsuValue *r = esu_db_exec(db, sql);
    esu_release(sql);
    if (r) esu_release(r);
}

void esu_db_rollback(EsuValue *db) {
    EsuValue *sql = esu_string("ROLLBACK");
    EsuValue *r = esu_db_exec(db, sql);
    esu_release(sql);
    if (r) esu_release(r);
}
