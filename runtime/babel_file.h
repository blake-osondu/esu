#ifndef BABEL_FILE_H
#define BABEL_FILE_H

#include "babel_value.h"

/* Read the entire contents of a file as a string */
BabelValue *babel_file_read(BabelValue *path);

/* Write a string to a file (overwrites) */
BabelValue *babel_file_write(BabelValue *path, BabelValue *content);

/* Append a string to a file */
BabelValue *babel_file_append(BabelValue *path, BabelValue *content);

/* Check if a file exists */
BabelValue *babel_file_exists(BabelValue *path);

/* Delete a file */
BabelValue *babel_file_delete(BabelValue *path);

/* Read file as a list of lines */
BabelValue *babel_file_lines(BabelValue *path);

/* Create an empty file (error if it already exists) */
BabelValue *babel_file_create(BabelValue *path);

/* Create or touch (no error if exists) */
BabelValue *babel_file_touch(BabelValue *path);

/* Move/rename a file */
BabelValue *babel_file_move(BabelValue *from, BabelValue *to);

/* Copy a file */
BabelValue *babel_file_copy(BabelValue *from, BabelValue *to);

/* Path builtins */
BabelValue *babel_home_dir(void);
BabelValue *babel_current_dir(void);
BabelValue *babel_temp_dir(void);
BabelValue *babel_desktop_dir(void);
BabelValue *babel_downloads_dir(void);
BabelValue *babel_documents_dir(void);

/* Expand ~ and $VAR in a path string */
BabelValue *babel_expand_path(BabelValue *path);

#endif
