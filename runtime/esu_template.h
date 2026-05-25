#ifndef ESU_TEMPLATE_H
#define ESU_TEMPLATE_H

#include "esu_value.h"

/* Render a template with a context map.
 *
 * `template_or_path` is either:
 *   - a literal template string (heuristic: contains '<' or '{')
 *   - a file path to load from disk and render
 *
 * `context` is a map; keys are referenced by `{{name}}` in the template,
 * and by `item` inside `{% for each item in list %}` loops.
 *
 * Template syntax (mustache-style braces; chosen so that Esu's own
 * single-brace `{name}` string interpolation doesn't collide):
 *   {{expr}}              — interpolate an expression (HTML-escaped)
 *   {{expr | raw}}        — interpolate without escaping
 *   {% if EXPR %} … {% else %} … {% end %}
 *   {% for each ITEM in EXPR %} … {% end %}
 *   {% include "PATH" %}  — inline another template (shared context)
 *   {# COMMENT #}         — stripped from output
 *
 * Returns the rendered string. Returns nothing on error (raises esu error).
 */
EsuValue *esu_render(EsuValue *template_or_path, EsuValue *context);

/* Explicit variants if the caller doesn't want auto-detection. */
EsuValue *esu_render_string(EsuValue *template_str, EsuValue *context);
EsuValue *esu_render_file(EsuValue *path, EsuValue *context);

/* HTML-escape a string. Caller frees. */
char *esu_html_escape(const char *s);

#endif
