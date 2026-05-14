#ifndef ESU_DB_H
#define ESU_DB_H

#include "esu_value.h"

/* Open a SQLite database file. Returns a "database handle" (a map with a
 * hidden __ctx pointer). Pass ":memory:" for an in-memory DB. */
EsuValue *esu_db_open(EsuValue *path);

/* Close the database. Rolls back any open transaction first. Idempotent. */
void esu_db_close(EsuValue *db);

/* Execute a non-SELECT statement (CREATE/INSERT/UPDATE/DELETE/PRAGMA/etc).
 * Returns {changes, last_id} — the row count and last inserted rowid. */
EsuValue *esu_db_exec(EsuValue *db, EsuValue *sql);

/* Same as exec, but with `?`-placeholder parameters bound from a list. */
EsuValue *esu_db_run(EsuValue *db, EsuValue *sql, EsuValue *params);

/* Run a SELECT (or any statement that yields rows). Returns a list of maps,
 * one per row, keyed by column name. */
EsuValue *esu_db_query(EsuValue *db, EsuValue *sql);
EsuValue *esu_db_query_with(EsuValue *db, EsuValue *sql, EsuValue *params);

/* Transactions: BEGIN / COMMIT / ROLLBACK shortcuts. */
void esu_db_begin(EsuValue *db);
void esu_db_commit(EsuValue *db);
void esu_db_rollback(EsuValue *db);

#endif
