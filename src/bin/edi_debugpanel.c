#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <Eo.h>
#include <Eina.h>
#include <Elementary.h>

#include "edi_debug.h"
#include "edi_debugpanel.h"
#include "edi_config.h"

#include "edi_private.h"

#if defined (__APPLE__)
 #define LIBTOOL_COMMAND "glibtool"
#else
 #define LIBTOOL_COMMAND "libtool"
#endif

static Evas_Object *_info_widget, *_entry_widget, *_button_start, *_button_quit;
static Evas_Object *_button_int, *_button_term;
static Elm_Code *_debug_output;

typedef struct _Edi_Debug {
   Edi_Debug_Tool *tool;
   const char *program_name;
   Ecore_Exe *exe;
   char cmd[1024];
} Edi_Debug;

static Edi_Debug *_debugger = NULL;

static void
_edi_debugpanel_line_cb(void *data EINA_UNUSED, const Efl_Event *event)
{
   Elm_Code_Line *line;

   line = (Elm_Code_Line *)event->info;
   if (line->data)
     line->status = ELM_CODE_STATUS_TYPE_ERROR;
}

static Eina_Bool
_edi_debugpanel_config_changed(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   elm_code_widget_font_set(_info_widget, _edi_project_config->font.name, _edi_project_config->font.size);

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_debugpanel_stdout_handler(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Exe_Event_Data *ev;
   int idx;
   char *start, *end = NULL;
   ev = event;

   if (ev->exe != _debugger->exe)
     return ECORE_CALLBACK_RENEW;

   if (ev && ev->size)
      {
         if (!ev->data) return ECORE_CALLBACK_DONE;

         char buf[ev->size + 1];
         memcpy(buf, ev->data, ev->size);
         buf[ev->size] = '\0';

         idx = 0;

         if (buf[idx] == '\n')
           idx++;

         start = &buf[idx];
         while (idx < ev->size)
           {
              if (buf[idx] == '\n')
                end = &buf[idx];

              if (start && end)
                {
                   elm_code_file_line_append(_debug_output->file, start, end - start, NULL);
                   start = end + 1;
                   end = NULL;
                }
              idx++;
           }
        /* We can forget the last line here as it's the prompt string */
    }

    return ECORE_CALLBACK_DONE;
}

static void
_edi_debugpanel_keypress_cb(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Key_Down *event;
   const char *text_markup;
   char *command, *text;
   Eina_Bool res;

   event = event_info;

   if (!event) return;

   if (!event->key) return;

   if (!strcmp(event->key, "Return"))
     {
        if (!_debugger->exe) return;

        text_markup = elm_object_part_text_get(_entry_widget, NULL);
        text = elm_entry_markup_to_utf8(text_markup);
        if (text)
          {
             command = malloc(strlen(text) + 2);
             snprintf(command, strlen(text) + 2, "%s\n", text);
             res = ecore_exe_send(_debugger->exe, command, strlen(command));
             if (res)
               elm_code_file_line_append(_debug_output->file, command, strlen(command) - 1, NULL);

             free(command);
             free(text);
          }
        elm_object_part_text_set(_entry_widget, NULL, "");
     }
}

static void
_edi_debugpanel_bt_sigterm_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   pid_t pid;
   Evas_Object *ico_int;

   pid = edi_debug_process_id(_debugger->exe, _debugger->program_name, NULL);
   if (pid <= 0) return;

   ico_int = elm_icon_add(_button_int);
   elm_icon_standard_set(ico_int, "media-playback-pause");
   elm_object_part_content_set(_button_int, "icon", ico_int);

   kill(pid, SIGTERM);
}

static void
_edi_debugpanel_icons_update(Edi_Debug_Process_State state)
{
   Evas_Object *ico_int;

   ico_int = elm_icon_add(_button_int);

   if (state == EDI_DEBUG_PROCESS_ACTIVE)
     elm_icon_standard_set(ico_int, "media-playback-pause");
   else
     elm_icon_standard_set(ico_int, "media-playback-start");

   elm_object_part_content_set(_button_int, "icon", ico_int);
}

static void
_edi_debugpanel_bt_sigint_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   pid_t pid;
   Edi_Debug_Process_State state;

   pid = edi_debug_process_id(_debugger->exe, _debugger->program_name, &state);
   if (pid <= 0) return;

   if (state == EDI_DEBUG_PROCESS_ACTIVE)
     kill(pid, SIGINT);
   else if (_debugger->tool->command_continue)
     ecore_exe_send(_debugger->exe, _debugger->tool->command_continue, strlen(_debugger->tool->command_continue));

    _edi_debugpanel_icons_update(state);
}

static void
_edi_debugpanel_button_quit_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   edi_debugpanel_stop();
}

static void
_edi_debugpanel_button_start_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   edi_debugpanel_start(_edi_project_config_debug_command_get());
}

static Eina_Bool
_edi_debug_active_check_cb(void *data EINA_UNUSED)
{
   Edi_Debug_Process_State state;
   int pid;

   pid = ecore_exe_pid_get(_debugger->exe);
   if (pid == -1)
     {
        if (_debugger->exe) ecore_exe_quit(_debugger->exe);
        _debugger->exe = NULL;
        elm_object_disabled_set(_button_quit, EINA_TRUE);
        elm_object_disabled_set(_button_start, EINA_FALSE);
        elm_object_disabled_set(_button_int, EINA_TRUE);
        elm_object_disabled_set(_button_term, EINA_TRUE);
     }
   else
     {
        if (edi_debug_process_id(_debugger->exe, _debugger->program_name, &state) > 0)
          _edi_debugpanel_icons_update(state);
     }

   return ECORE_CALLBACK_RENEW;
}

void edi_debugpanel_stop(void)
{
   int pid;

   if (_debugger->exe)
     ecore_exe_terminate(_debugger->exe);

   pid = ecore_exe_pid_get(_debugger->exe);
   if (pid != -1)
     ecore_exe_quit(_debugger->exe);

   _debugger->exe = NULL;

   elm_object_disabled_set(_button_quit, EINA_TRUE);
   elm_object_disabled_set(_button_int, EINA_TRUE);
   elm_object_disabled_set(_button_term, EINA_TRUE);
}

static void
_edi_debugger_run(void)
{
   const char *fmt;
   char *args;
   int len;

   _debugger->exe = ecore_exe_pipe_run(_debugger->cmd, ECORE_EXE_PIPE_WRITE |
                                                       ECORE_EXE_PIPE_ERROR |
                                                       ECORE_EXE_PIPE_READ, NULL);

   if (_debugger->tool->command_arguments && _edi_project_config->launch.args)
     {
        fmt = _debugger->tool->command_arguments;
        len = strlen(fmt) + strlen(_edi_project_config->launch.args) + 1;
        args = malloc(len);
        snprintf(args, len, fmt, _edi_project_config->launch.args);
        ecore_exe_send(_debugger->exe, args, strlen(args));
        free(args);
     }

   if (_debugger->tool->command_start)
     ecore_exe_send(_debugger->exe, _debugger->tool->command_start, strlen(_debugger->tool->command_start));
}

void edi_debugpanel_start(const char *toolname)
{
   const char *mime;

   if (_debugger->exe) return;

   if (!_edi_project_config->launch.path)
     {
        edi_launcher_config_missing();
        return;
     }

   _debugger->tool = edi_debug_tool_get(toolname);
   if (!_debugger->tool)
     {
        edi_debug_exe_missing();
        return;
     }

   _debugger->program_name = ecore_file_file_get(_edi_project_config->launch.path);

   mime = efreet_mime_type_get(_edi_project_config->launch.path);
   if (mime && !strcmp(mime, "application/x-shellscript"))
     snprintf(_debugger->cmd, sizeof(_debugger->cmd), LIBTOOL_COMMAND " --mode execute %s %s", _debugger->tool->exec, _edi_project_config->launch.path);
   else if (_debugger->tool->arguments)
     snprintf(_debugger->cmd, sizeof(_debugger->cmd), "%s %s %s", _debugger->tool->exec, _debugger->tool->arguments, _edi_project_config->launch.path);
   else
     snprintf(_debugger->cmd, sizeof(_debugger->cmd), "%s %s", _debugger->tool->exec, _edi_project_config->launch.path);

   elm_object_disabled_set(_button_int, EINA_FALSE);
   elm_object_disabled_set(_button_term, EINA_FALSE);
   elm_object_disabled_set(_button_quit, EINA_FALSE);
   elm_object_disabled_set(_button_start, EINA_TRUE);

   _edi_debugger_run();
}

void edi_debugpanel_add(Evas_Object *parent)
{
   Evas_Object *table, *entry, *bt_term, *bt_int, *bt_start, *bt_quit;
   Evas_Object *separator;
   Evas_Object *ico_start, *ico_quit, *ico_int, *ico_term;
   Elm_Code_Widget *widget;
   Elm_Code *code;
   Ecore_Timer *timer;

   code = elm_code_create();
   widget = elm_code_widget_add(parent, code);
   elm_obj_code_widget_font_set(widget, _edi_project_config->font.name, _edi_project_config->font.size);
   elm_obj_code_widget_gravity_set(widget, 0.0, 1.0);
   efl_event_callback_add(widget, &ELM_CODE_EVENT_LINE_LOAD_DONE, _edi_debugpanel_line_cb, NULL);
   evas_object_size_hint_weight_set(widget, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(widget, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(widget);

   table = elm_table_add(parent);
   evas_object_size_hint_weight_set(table, EVAS_HINT_EXPAND, 0);
   evas_object_size_hint_align_set(table, EVAS_HINT_FILL, 0);

   separator = elm_separator_add(parent);
   elm_separator_horizontal_set(separator, EINA_FALSE);
   evas_object_show(separator);

   _button_term = bt_term = elm_button_add(parent);
   ico_term = elm_icon_add(parent);
   elm_icon_standard_set(ico_term, "media-playback-stop");
   elm_object_part_content_set(bt_term, "icon", ico_term);
   elm_object_tooltip_text_set(bt_term, "Send SIGTERM");
   elm_object_disabled_set(bt_term, EINA_TRUE);
   evas_object_smart_callback_add(bt_term, "clicked", _edi_debugpanel_bt_sigterm_cb, NULL);
   evas_object_show(bt_term);

   _button_int = bt_int = elm_button_add(parent);
   ico_int = elm_icon_add(parent);
   elm_icon_standard_set(ico_int, "media-playback-pause");
   elm_object_part_content_set(bt_int, "icon", ico_int);
   elm_object_tooltip_text_set(bt_int, "Start/Stop Process");
   elm_object_disabled_set(bt_int, EINA_TRUE);
   evas_object_smart_callback_add(bt_int, "clicked", _edi_debugpanel_bt_sigint_cb, NULL);
   evas_object_show(bt_int);

   _button_start = bt_start = elm_button_add(parent);
   ico_start = elm_icon_add(parent);
   elm_icon_standard_set(ico_start, "media-playback-start");
   elm_object_tooltip_text_set(bt_start, "Start Debugging");
   elm_object_part_content_set(bt_start, "icon", ico_start);
   evas_object_smart_callback_add(bt_start, "clicked", _edi_debugpanel_button_start_cb, NULL);
   evas_object_show(bt_start);

   _button_quit = bt_quit = elm_button_add(parent);
   ico_quit = elm_icon_add(parent);
   elm_icon_standard_set(ico_quit, "application-exit");
   elm_object_part_content_set(bt_quit, "icon", ico_quit);
   elm_object_tooltip_text_set(bt_quit, "Stop Debugging");
   elm_object_disabled_set(bt_quit, EINA_TRUE);
   evas_object_smart_callback_add(bt_quit, "clicked", _edi_debugpanel_button_quit_cb, NULL);
   evas_object_show(bt_quit);

   entry = elm_entry_add(parent);
   elm_entry_single_line_set(entry, EINA_TRUE);
   elm_entry_scrollable_set(entry, EINA_TRUE);
   elm_entry_editable_set(entry, EINA_TRUE);
   evas_object_size_hint_weight_set(entry, EVAS_HINT_EXPAND, 0);
   evas_object_size_hint_align_set(entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_event_callback_add(entry, EVAS_CALLBACK_KEY_DOWN, _edi_debugpanel_keypress_cb, NULL);
   evas_object_show(entry);

   elm_table_pack(table, entry, 0, 0, 1, 1);
   elm_table_pack(table, bt_term, 1, 0, 1, 1);
   elm_table_pack(table, bt_int, 2, 0, 1, 1);
   elm_table_pack(table, separator, 3, 0, 1, 1);
   elm_table_pack(table, bt_start, 4, 0, 1, 1);
   elm_table_pack(table, bt_quit, 5, 0, 1, 1);
   evas_object_show(table);

   _debug_output = code;
   _info_widget = widget;
   _entry_widget = entry;

   _debugger = calloc(1, sizeof(Edi_Debug));

   timer = ecore_timer_add(1.0, _edi_debug_active_check_cb, NULL);
   (void) timer;

   elm_box_pack_end(parent, widget);
   elm_box_pack_end(parent, table);

   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _debugpanel_stdout_handler, NULL);
   ecore_event_handler_add(ECORE_EXE_EVENT_ERROR, _debugpanel_stdout_handler, NULL);
   ecore_event_handler_add(EDI_EVENT_CONFIG_CHANGED, _edi_debugpanel_config_changed, NULL);
}
