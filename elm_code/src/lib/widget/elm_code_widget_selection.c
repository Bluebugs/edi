#ifdef HAVE_CONFIG
# include "config.h"
#endif

#include "Elm_Code.h"

#include "elm_code_widget_private.h"

static char _breaking_chars[] = " \t,.?!;:()[]{}";

static Elm_Code_Widget_Selection_Data *
_elm_code_widget_selection_new()
{
   Elm_Code_Widget_Selection_Data *data;

   data = calloc(1, sizeof(Elm_Code_Widget_Selection_Data));

   return data;
}

static void
_elm_code_widget_selection_limit(Evas_Object *widget EINA_UNUSED, Elm_Code_Widget_Data *pd,
                                 unsigned int *row, unsigned int *col)
{
   Elm_Code_Line *line;
   Elm_Code_File *file;
   unsigned int width;

   file = pd->code->file;

   if (*row > elm_code_file_lines_get(file))
     *row = elm_code_file_lines_get(file);

   line = elm_code_file_line_get(file, *row);
   width = elm_code_widget_line_text_column_width_get(widget, line);

   if (*col > width + 1)
     *col = width + 1;
   if (*col < 1)
     *col = 1;
}

EAPI void
elm_code_widget_selection_start(Evas_Object *widget,
                                unsigned int line, unsigned int col)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Widget_Selection_Data *selection;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   _elm_code_widget_selection_limit(widget, pd, &line, &col);
   if (!pd->selection)
     {
        selection = _elm_code_widget_selection_new();

        selection->end_line = line;
        selection->end_col = col;

        pd->selection = selection;
     }

   pd->selection->start_line = line;
   pd->selection->start_col = col;
   eo_do(widget,
         eo_event_callback_call(ELM_CODE_WIDGET_EVENT_SELECTION_CHANGED, widget),
         elm_obj_code_widget_cursor_position_set(col, line));
}

EAPI void
elm_code_widget_selection_end(Evas_Object *widget,
                              unsigned int line, unsigned int col)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Widget_Selection_Data *selection;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   _elm_code_widget_selection_limit(widget, pd, &line, &col);
   if (!pd->selection)
     {
        selection = _elm_code_widget_selection_new();

        selection->start_line = line;
        selection->start_col = col;

        pd->selection = selection;
     }

   pd->selection->end_line = line;
   pd->selection->end_col = col;
   eo_do(widget, eo_event_callback_call(ELM_CODE_WIDGET_EVENT_SELECTION_CHANGED, widget));
}

EAPI Elm_Code_Widget_Selection_Data *
elm_code_widget_selection_normalized_get(Evas_Object *widget)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Widget_Selection_Data *selection;
   Eina_Bool reverse;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);
   selection = _elm_code_widget_selection_new();

   if (!pd->selection)
     {
        selection->start_line = selection->end_line = 1;
        selection->start_col = selection->end_col = 1;

        return selection;
     }

   if (pd->selection->start_line == pd->selection->end_line)
     reverse = pd->selection->start_col > pd->selection->end_col;
   else
     reverse = pd->selection->start_line > pd->selection->end_line;

   if (reverse)
     {
        selection->start_line = pd->selection->end_line;
        selection->start_col = pd->selection->end_col;
        selection->end_line = pd->selection->start_line;
        selection->end_col = pd->selection->start_col;
     }
   else
     {
        selection->start_line = pd->selection->start_line;
        selection->start_col = pd->selection->start_col;
        selection->end_line = pd->selection->end_line;
        selection->end_col = pd->selection->end_col;
     }

   return selection;
}

EAPI void
elm_code_widget_selection_clear(Evas_Object *widget)
{
   Elm_Code_Widget_Data *pd;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   if (pd->selection)
     free(pd->selection);

   pd->selection = NULL;
   eo_do(widget, eo_event_callback_call(ELM_CODE_WIDGET_EVENT_SELECTION_CLEARED, widget));
}

static void
_elm_code_widget_selection_delete_single(Elm_Code_Widget *widget, Elm_Code_Widget_Data *pd)
{
   Elm_Code_Line *line;
   const char *old;
   unsigned int old_length, start, end, length;
   char *content;
   Elm_Code_Widget_Selection_Data *selection;

   selection = elm_code_widget_selection_normalized_get(widget);
   line = elm_code_file_line_get(pd->code->file, selection->start_line);
   old = elm_code_line_text_get(line, &old_length);
   start = elm_code_widget_line_text_position_for_column_get(widget, line, selection->start_col);
   end = elm_code_widget_line_text_position_for_column_get(widget, line, selection->end_col);
   length = line->length - (end - start + 1);

   content = malloc(sizeof(char) * length);
   strncpy(content, old, start);
   strncpy(content + start, old + end + 1,
           old_length - (end + 1));
   elm_code_line_text_set(line, content, length);
   free(content);
   free(selection);
}

static void
_elm_code_widget_selection_delete_multi(Elm_Code_Widget *widget, Elm_Code_Widget_Data *pd)
{
   Elm_Code_Line *line;
   const char *first, *last;
   unsigned int last_length, start, end, length, i;
   char *content;
   Elm_Code_Widget_Selection_Data *selection;

   if (pd->selection->end_line == pd->selection->start_line)
     return;

   selection = elm_code_widget_selection_normalized_get(widget);
   line = elm_code_file_line_get(pd->code->file, selection->start_line);
   first = elm_code_line_text_get(line, NULL);
   start = elm_code_widget_line_text_position_for_column_get(widget, line, selection->start_col);

   line = elm_code_file_line_get(pd->code->file, selection->end_line);
   last = elm_code_line_text_get(line, &last_length);
   end = elm_code_widget_line_text_position_for_column_get(widget, line, selection->end_col);

   length = start + last_length - (end + 1);
   content = malloc(sizeof(char) * length);
   strncpy(content, first, start);
   strncpy(content + start, last + end + 1,
           last_length - (end + 1));

   for (i = line->number; i > selection->start_line; i--)
     elm_code_file_line_remove(pd->code->file, i);

   line = elm_code_file_line_get(pd->code->file, selection->start_line);
   elm_code_line_text_set(line, content, length);
   free(content);
   free(selection);
}

EAPI void
elm_code_widget_selection_delete(Evas_Object *widget)
{
   Elm_Code_Widget_Data *pd;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   if (!pd->selection)
     return;

   if (pd->selection->start_line == pd->selection->end_line)
     _elm_code_widget_selection_delete_single(widget, pd);
   else
     _elm_code_widget_selection_delete_multi(widget, pd);

   free(pd->selection);
   pd->selection = NULL;
   eo_do(widget, eo_event_callback_call(ELM_CODE_WIDGET_EVENT_SELECTION_CLEARED, widget));
}

EAPI void
elm_code_widget_selection_select_line(Evas_Object *widget, unsigned int line)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Line *lineobj;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);
   lineobj = elm_code_file_line_get(pd->code->file, line);

   if (!lineobj)
     return;

   elm_code_widget_selection_start(widget, line, 1);
   elm_code_widget_selection_end(widget, line, lineobj->length);
}

static Eina_Bool
_elm_code_widget_selection_char_breaks(char chr)
{
   unsigned int i;

   if (chr == 0)
     return EINA_TRUE;

   for (i = 0; i < sizeof(_breaking_chars); i++)
     if (chr == _breaking_chars[i])
       return EINA_TRUE;


   return EINA_FALSE;
}

EAPI void
elm_code_widget_selection_select_word(Evas_Object *widget, unsigned int line, unsigned int col)
{
   Elm_Code_Widget_Data *pd;
   Elm_Code_Line *lineobj;
   unsigned int colpos, length, pos;
   const char *content;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);
   lineobj = elm_code_file_line_get(pd->code->file, line);
   content = elm_code_line_text_get(lineobj, &length);

   _elm_code_widget_selection_limit(widget, pd, &line, &col);
   colpos = elm_code_widget_line_text_position_for_column_get(widget, lineobj, col);

   pos = colpos;
   while (pos > 0)
     {
        if (_elm_code_widget_selection_char_breaks(content[pos - 1]))
          break;
        pos--;
     }
   elm_code_widget_selection_start(widget, line,
                                   elm_code_widget_line_text_column_width_to_position(widget, lineobj, pos));

   pos = colpos;
   while (pos < length - 1)
     {
        if (_elm_code_widget_selection_char_breaks(content[pos + 1]))
          break;
        pos++;
     }
   elm_code_widget_selection_end(widget, line,
                                 elm_code_widget_line_text_column_width_to_position(widget, lineobj, pos));
}

static char *
_elm_code_widget_selection_text_single_get(Elm_Code_Widget *widget, Elm_Code_Widget_Data *pd)
{
   Elm_Code_Line *line;
   unsigned int start, end;
   Elm_Code_Widget_Selection_Data *selection;

   selection = elm_code_widget_selection_normalized_get(widget);
   line = elm_code_file_line_get(pd->code->file, selection->start_line);
   start = elm_code_widget_line_text_position_for_column_get(widget, line, selection->start_col);
   end = elm_code_widget_line_text_position_for_column_get(widget, line, selection->end_col + 1);
   free(selection);

   return elm_code_line_text_substr(line, start, end - start);
}

static char *
_elm_code_widget_selection_text_multi_get(Elm_Code_Widget *widget, Elm_Code_Widget_Data *pd)
{
   Elm_Code_Line *line;
   char *first, *last, *ret, *ptr;
   const char *newline;
   short newline_len;
   int ret_len;
   unsigned int row, start, end;
   Elm_Code_Widget_Selection_Data *selection;

   selection = elm_code_widget_selection_normalized_get(widget);
   newline = elm_code_file_line_ending_chars_get(pd->code->file, &newline_len);

   line = elm_code_file_line_get(pd->code->file, selection->start_line);
   start = elm_code_widget_line_text_position_for_column_get(widget, line, selection->start_col);
   first = elm_code_line_text_substr(line, start, line->length - start + 1);

   line = elm_code_file_line_get(pd->code->file, selection->end_line);
   end = elm_code_widget_line_text_position_for_column_get(widget, line, selection->end_col + 1);
   last = elm_code_line_text_substr(line, 0, end);

   ret_len = strlen(first) + strlen(last) + newline_len;

   for (row = pd->selection->start_line + 1; row < selection->end_line; row++)
     {
        line = elm_code_file_line_get(pd->code->file, row);
        ret_len += line->length + newline_len;
     }

   ret = malloc(sizeof(char) * (ret_len + 1));
   snprintf(ret, strlen(first) + newline_len + 1, "%s%s", first, newline);

   ptr = ret;
   ptr += strlen(first) + newline_len;

   for (row = selection->start_line + 1; row < selection->end_line; row++)
     {
        line = elm_code_file_line_get(pd->code->file, row);
        if (line->modified)
          snprintf(ptr, line->length + 1, "%s", line->modified);
        else
          snprintf(ptr, line->length + 1, "%s", line->content);

        snprintf(ptr + line->length, newline_len + 1, "%s", newline);
        ptr += line->length + newline_len;
     }
   snprintf(ptr, strlen(last) + 1, "%s", last);

   free(selection);
   free(first);
   free(last);
   return ret;
}

EAPI char *
elm_code_widget_selection_text_get(Evas_Object *widget)
{
   Elm_Code_Widget_Data *pd;

   pd = eo_data_scope_get(widget, ELM_CODE_WIDGET_CLASS);

   if (!pd->selection)
     return strdup("");

   if (pd->selection->start_line == pd->selection->end_line)
     return _elm_code_widget_selection_text_single_get(widget, pd);
   else
     return _elm_code_widget_selection_text_multi_get(widget, pd);
}

static void
_selection_loss_cb(void *data, Elm_Sel_Type selection EINA_UNUSED)
{
   Elm_Code_Widget *widget;

   widget = (Elm_Code_Widget *)data;
// TODO we need to know whih selection we are clearing!
//   elm_code_widget_selection_clear(widget);
}

EAPI void
elm_code_widget_selection_cut(Evas_Object *widget)
{
   char *text;

   text = elm_code_widget_selection_text_get(widget);
   elm_cnp_selection_set(widget, ELM_SEL_TYPE_CLIPBOARD, ELM_SEL_FORMAT_TEXT, text, strlen(text));
   elm_cnp_selection_loss_callback_set(widget, ELM_SEL_TYPE_CLIPBOARD, _selection_loss_cb, widget);
   free(text);

   elm_code_widget_selection_delete(widget);
}

EAPI void
elm_code_widget_selection_copy(Evas_Object *widget)
{
   char *text;

   text = elm_code_widget_selection_text_get(widget);
   elm_cnp_selection_set(widget, ELM_SEL_TYPE_CLIPBOARD, ELM_SEL_FORMAT_TEXT, text, strlen(text));
   elm_cnp_selection_loss_callback_set(widget, ELM_SEL_TYPE_CLIPBOARD, _selection_loss_cb, widget);
   free(text);
}

static void
_selection_paste_single(Elm_Code_Widget *widget, Elm_Code *code,
                        unsigned int col, unsigned int row, const char *text, unsigned int len)
{
   Elm_Code_Line *line;
   unsigned int position, newcol;

   line = elm_code_file_line_get(code->file, row);
   position = elm_code_widget_line_text_position_for_column_get(widget, line, col);
   elm_code_line_text_insert(line, position, text, len);

   newcol = elm_code_widget_line_text_column_width_to_position(widget, line, position + len);
   eo_do(widget,
         elm_obj_code_widget_cursor_position_set(newcol, row));
}

static void
_selection_paste_multi(Elm_Code_Widget *widget, Elm_Code *code,
                       unsigned int col, unsigned int row, const char *text, unsigned int len)
{
   Elm_Code_Line *line;
   unsigned int position, newrow, remain;
   int nlpos;
   short nllen;
   char *ptr;

   line = elm_code_file_line_get(code->file, row);
   position = elm_code_widget_line_text_position_for_column_get(widget, line, col);
   elm_code_line_split_at(line, position);

   newrow = row;
   ptr = (char *)text;
   remain = len;
   while ((nlpos = elm_code_text_newlinenpos(ptr, remain, &nllen)) != ELM_CODE_TEXT_NOT_FOUND)
     {
        if (newrow == row)
          _selection_paste_single(widget, code, col, row, text, nlpos);
        else
          elm_code_file_line_insert(code->file, newrow, ptr, nlpos, NULL);

        remain -= nlpos + nllen;
        ptr += nlpos + nllen;
        newrow++;
     }

   _selection_paste_single(widget, code, 1, newrow, ptr, len - (ptr - text));
}

static Eina_Bool
_selection_paste_cb(void *data, Evas_Object *obj EINA_UNUSED, Elm_Selection_Data *ev)
{
   Elm_Code *code;
   Elm_Code_Widget *widget;
   unsigned int row, col;

   widget = (Elm_Code_Widget *)data;

   if (ev->format != ELM_SEL_FORMAT_TEXT)
     return EINA_TRUE;
   if (ev->len <= 0)
     return EINA_TRUE;

   eo_do(widget,
         code = elm_obj_code_widget_code_get(),
         elm_obj_code_widget_cursor_position_get(&col, &row));

   if (elm_code_text_newlinenpos(ev->data, ev->len, NULL) == ELM_CODE_TEXT_NOT_FOUND)
     _selection_paste_single(widget, code, col, row, ev->data, ev->len - 1);
   else
     _selection_paste_multi(widget, code, col, row, ev->data, ev->len - 1);

   return EINA_TRUE;
}

EAPI void
elm_code_widget_selection_paste(Evas_Object *widget)
{
   elm_code_widget_selection_delete(widget);

   elm_cnp_selection_get(widget, ELM_SEL_TYPE_CLIPBOARD, ELM_SEL_FORMAT_TEXT, _selection_paste_cb, widget);
}
