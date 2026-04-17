#ifndef ESU_FILE_H
#define ESU_FILE_H

#include "esu_value.h"

/* Read the entire contents of a file as a string */
EsuValue *esu_file_read(EsuValue *path);

/* Write a string to a file (overwrites) */
EsuValue *esu_file_write(EsuValue *path, EsuValue *content);

/* Append a string to a file */
EsuValue *esu_file_append(EsuValue *path, EsuValue *content);

/* Check if a file exists */
EsuValue *esu_file_exists(EsuValue *path);

/* Delete a file */
EsuValue *esu_file_delete(EsuValue *path);

/* Read file as a list of lines */
EsuValue *esu_file_lines(EsuValue *path);

/* Create an empty file (error if it already exists) */
EsuValue *esu_file_create(EsuValue *path);

/* Create or touch (no error if exists) */
EsuValue *esu_file_touch(EsuValue *path);

/* Move/rename a file */
EsuValue *esu_file_move(EsuValue *from, EsuValue *to);

/* Copy a file */
EsuValue *esu_file_copy(EsuValue *from, EsuValue *to);

/* Path builtins */
EsuValue *esu_home_dir(void);
EsuValue *esu_current_dir(void);
EsuValue *esu_temp_dir(void);
EsuValue *esu_desktop_dir(void);
EsuValue *esu_downloads_dir(void);
EsuValue *esu_documents_dir(void);

/* Expand ~ and $VAR in a path string */
EsuValue *esu_expand_path(EsuValue *path);

#endif
