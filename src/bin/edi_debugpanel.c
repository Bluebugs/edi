#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined (__APPLE__) || defined(__OpenBSD__)
 #include <unistd.h>
 #include <sys/types.h>
 #include <sys/sysctl.h>
 #include <sys/user.h>
#endif
#if defined(__OpenBSD__)
 #include <sys/proc.h>
#endif

#include <Eo.h>
#include <Eina.h>
#include <Elementary.h>

#include "edi_debugpanel.h"
#include "edi_config.h"

#include "edi_private.h"

#if defined (__APPLE__)
 #define LIBTOOL_COMMAND "glibtool"
#else
 #define LIBTOOL_COMMAND "libtool"
#endif

static Ecore_Exe *_debug_exe = NULL;
static Evas_Object *_info_widget, *_entry_widget, *_button_start, *_button_quit;
static Evas_Object *_button_int, *_button_term;
static Elm_Code *_debug_output;

#define DEBUG_PROCESS_SLEEPING 0
#define DEBUG_PROCESS_ACTIVE 1

typedef struct _Edi_Debug_Tool {
   const char *name;
   const char *exec;
   const char *arguments;
   const char *command_start;
   const char *command_continue;
   const char *command_arguments;
} Edi_Debug_Tool;

static Edi_Debug_Tool _debugger_tools[] = {
    { "gdb", "gdb", NULL, "run\n", "c\n", "set args %s" },
    { "lldb", "lldb", NULL, "run\n", "c\n", "settings set target.run-args %s" },
    { "pdb", "pdb", NULL, NULL, "c\n", "run %s" },
    { "memcheck", "valgrind", "--tool=memcheck", NULL, NULL, NULL },
    { "massif", "valgrind", "--tool=massif", NULL, NULL, NULL },
    { "callgrind", "valgrind", "--tool=callgrind", NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL, NULL, NULL },
};

static Edi_Debug_Tool *_debugger = NULL;
static char _debugger_cmd[1024];

static Edi_Debug_Tool *
_edi_debug_tool_get(const char *name)
{
   int i;

   for (i = 0; _debugger_tools[i].name; i++)
      {
         if (!strcmp(_debugger_tools[i].name, name))
           {
              _debugger = &_debugger_tools[i];
              return _debugger;
           }
      }

    return NULL;
}

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
        if (!_debug_exe) return;

        text_markup = elm_object_part_text_get(_entry_widget, NULL);
        text = elm_entry_markup_to_utf8(text_markup);
        if (text)
          {
             command = malloc(strlen(text) + 2);
             snprintf(command, strlen(text) + 2, "%s\n", text);
             res = ecore_exe_send(_debug_exe, command, strlen(command));
             if (res)
               elm_code_file_line_append(_debug_output->file, command, strlen(command) - 1, NULL);

             free(command);
             free(text);
          }
        elm_object_part_text_set(_entry_widget, NULL, "");
     }
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
static long int
_sysctlfromname(const char *name, void *mib, int depth, size_t *len)
{
   long int result;

   if (sysctlnametomib(name, mib, len) < 0) return -1;
   *len = sizeof(result);
   if (sysctl(mib, depth, &result, len, NULL, 0) < 0) return -1;

   return result;
}
#endif

/* Get the process ID of the child process being debugged in *our* session */
static int
_edi_debug_process_id(int *state)
{
   const char *program_name;
   int my_pid, child_pid = -1;
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined (__APPLE__) || defined(__OpenBSD__)
   struct kinfo_proc kp;
   int mib[6];
   size_t len;
   int max_pid, i;

   if (!_edi_project_config->launch.path) return -1;

   if (!_debug_exe) return -1;

#if defined(__FreeBSD__) || defined(__DragonFly__)
   len = sizeof(max_pid);
   max_pid = _sysctlfromname("kern.lastpid", mib, 2, &len);
#elif defined(PID_MAX)
   max_pid = PID_MAX;
#else
   max_pid = 99999;
#endif
   my_pid = ecore_exe_pid_get(_debug_exe);

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__APPLE__)
   if (sysctlnametomib("kern.proc.pid", mib, &len) < 0) return -1;
#elif defined(__OpenBSD__)
   mib[0] = CTL_KERN;
   mib[1] = KERN_PROC;
   mib[2] = KERN_PROC_PID;
   mib[4] = sizeof(struct kinfo_proc);
   mib[5] = 1;
#endif
   program_name = ecore_file_file_get(_edi_project_config->launch.path);

   for (i = my_pid; i <= max_pid; i++)
     {
        mib[3] = i;
        len = sizeof(kp);
#if defined(__OpenBSD__)
        if (sysctl(mib, 6, &kp, &len, NULL, 0) == -1) continue;
#else
        if (sysctl(mib, 4, &kp, &len, NULL, 0) == -1) continue;
#endif

#if defined(__FreeBSD__) || defined(__DragonFly__)
        if (kp.ki_ppid != my_pid) continue;
        if (strcmp(program_name, kp.ki_comm)) continue;
        child_pid = kp.ki_pid;
        if (state)
          {
             if (kp.ki_stat == SRUN || kp.ki_stat == SSLEEP)
               *state = DEBUG_PROCESS_ACTIVE;
             else
               *state = DEBUG_PROCESS_SLEEPING;
          }
#elif defined(__OpenBSD__)
        if (kp.p_ppid != my_pid) continue;
        if (strcmp(program_name, kp.p_comm)) continue;
        child_pid = kp.p_pid;

        if (state)
          {
             if (kp.p_stat == SRUN || kp.p_stat == SSLEEP)
               *state = DEBUG_PROCESS_ACTIVE;
             else
               *state = DEBUG_PROCESS_SLEEPING;
          }
#else /* APPLE */
        if (kp.kp_proc.p_oppid != my_pid) continue;
        if (strcmp(program_name, kp.kp_proc.p_comm)) continue;
        child_pid = kp.kp_proc.p_pid;
        if (state)
          {
             if (kp.kp_proc.p_stat == SRUN || kp.kp_proc.p_stat == SSLEEP)
               *state = DEBUG_PROCESS_ACTIVE;
             else
               *state = DEBUG_PROCESS_SLEEPING;
          }
#endif
        break;
     }
#else
   Eina_List *files, *l;
   const char *temp_name;
   char path[PATH_MAX];
   char buf[4096];
   char  *p, *name, *end;
   FILE *f;
   int count, parent_pid, pid;

   if (!_edi_project_config->launch.path)
     return -1;

   if (!_debug_exe) return -1;

   my_pid = ecore_exe_pid_get(_debug_exe);

   program_name = ecore_file_file_get(_edi_project_config->launch.path);

   files = ecore_file_ls("/proc");

   EINA_LIST_FOREACH(files, l, name)
     {
        pid = atoi(name);
        if (!pid) continue;
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        f = fopen(path, "r");
        if (!f) continue;
        p = fgets(buf, sizeof(buf), f);
        fclose(f);
        if (!p) continue;
        temp_name = ecore_file_file_get(buf);
        if (!strcmp(temp_name, program_name))
          {
             parent_pid = 0;
             // Match success - program name with pid.
             child_pid = pid;
             snprintf(path, sizeof(path), "/proc/%d/stat", pid);
             f = fopen(path, "r");
             if (f)
               {
                  count = 0;
                  p = fgets(buf, sizeof(buf), f);
                  while (*p++ != '\0')
                    {
                       if (p[0] == ' ') { count++; p++; }
                       if (count == 2)
                         {
                            if (state)
                              {
                                 if (p[0] == 'S' || p[0] == 'R')
                                   *state = DEBUG_PROCESS_ACTIVE;
                                 else
                                   *state = DEBUG_PROCESS_SLEEPING;
                              }
                         }
                       if (count == 3) break;
                    }
                  end = strchr(p, ' ');
                  if (end)
                    {
                       *end = '\0';
                       // parent pid matches - right process.
                       parent_pid = atoi(p);
                    }
                  fclose(f);
               }
             if (parent_pid == my_pid)
               break;
          }
     }

   if (files)
     eina_list_free(files);
#endif
   return child_pid;
}

static void
_edi_debugpanel_bt_sigterm_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   pid_t pid;
   Evas_Object *ico_int;

   pid = _edi_debug_process_id(NULL);
   if (pid <= 0) return;

   ico_int = elm_icon_add(_button_int);
   elm_icon_standard_set(ico_int, "media-playback-pause");
   elm_object_part_content_set(_button_int, "icon", ico_int);

   kill(pid, SIGTERM);
}

static void
_edi_debugpanel_icons_update(int state)
{
   Evas_Object *ico_int;

   ico_int = elm_icon_add(_button_int);

   if (state == DEBUG_PROCESS_ACTIVE)
     elm_icon_standard_set(ico_int, "media-playback-pause");
   else
     elm_icon_standard_set(ico_int, "media-playback-start");

   elm_object_part_content_set(_button_int, "icon", ico_int);
}

static void
_edi_debugpanel_bt_sigint_cb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   pid_t pid;
   int state;

   pid = _edi_debug_process_id(&state);
   if (pid <= 0) return;

   if (state == DEBUG_PROCESS_ACTIVE)
     kill(pid, SIGINT);
   else if (_debugger->command_continue)
     ecore_exe_send(_debug_exe, _debugger->command_continue, strlen(_debugger->command_continue));

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
   const char *cmd = _edi_project_config_debug_command_get();
   if (!cmd) return;

   edi_debugpanel_start(cmd);
}

static Eina_Bool
_edi_debug_active_check_cb(void *data EINA_UNUSED)
{
   int state, pid = ecore_exe_pid_get(_debug_exe);

   if (pid == -1)
     {
        if (_debug_exe) ecore_exe_quit(_debug_exe);
        _debug_exe = NULL;
        elm_object_disabled_set(_button_quit, EINA_TRUE);
        elm_object_disabled_set(_button_start, EINA_FALSE);
        elm_object_disabled_set(_button_int, EINA_TRUE);
        elm_object_disabled_set(_button_term, EINA_TRUE);
     }
   else
     {
        if (_edi_debug_process_id(&state) > 0)
          _edi_debugpanel_icons_update(state);
     }

   return ECORE_CALLBACK_RENEW;
}

void edi_debugpanel_stop(void)
{
   int pid;

   if (_debug_exe)
     ecore_exe_terminate(_debug_exe);

   pid = ecore_exe_pid_get(_debug_exe);
   if (pid != -1)
     ecore_exe_quit(_debug_exe);

   _debug_exe = NULL;

   elm_object_disabled_set(_button_quit, EINA_TRUE);
   elm_object_disabled_set(_button_int, EINA_TRUE);
   elm_object_disabled_set(_button_term, EINA_TRUE);
}

static void
_edi_debugger_run(Edi_Debug_Tool *tool)
{
   const char *fmt;
   char *args;
   int len;

   _debug_exe = ecore_exe_pipe_run(_debugger_cmd, ECORE_EXE_PIPE_WRITE |
                                                  ECORE_EXE_PIPE_ERROR |
                                                  ECORE_EXE_PIPE_READ, NULL);

   ecore_event_handler_add(ECORE_EXE_EVENT_DATA, _debugpanel_stdout_handler, NULL);
   ecore_event_handler_add(ECORE_EXE_EVENT_ERROR, _debugpanel_stdout_handler, NULL);

   if (tool->command_arguments && _edi_project_config->launch.args)
     {
        fmt = _debugger->command_arguments;
        len = strlen(fmt) + strlen(_edi_project_config->launch.args) + 1;
        args = malloc(len);
        snprintf(args, len, fmt, _edi_project_config->launch.args);
        ecore_exe_send(_debug_exe, args, strlen(args));
        free(args);
     }

   if (_debugger->command_start)
     ecore_exe_send(_debug_exe, _debugger->command_start, strlen(_debugger->command_start));
}

void edi_debugpanel_start(const char *toolname)
{
   Edi_Debug_Tool *tool;
   const char *warning, *mime;

   if (!_edi_project_config->launch.path)
     {
        edi_launcher_config_missing();
        return;
     }

   if (_debug_exe) return;

   if (!ecore_file_exists(_edi_project_config->launch.path))
     {
        warning = _("Warning: executable does not exists (run make?)");
        elm_code_file_line_append(_debug_output->file, warning, strlen(warning), NULL);
        return;
     }

   tool = _edi_debug_tool_get(toolname);
   if (!tool || !ecore_file_app_installed(tool->exec))
     {
        warning = _("Warning: debug tool is not installed (check settings and system configuration).");
        elm_code_file_line_append(_debug_output->file, warning, strlen(warning), NULL);
        return;
     }

   mime = efreet_mime_type_get(_edi_project_config->launch.path);
   if (!strcmp(mime, "application/x-shellscript"))
     snprintf(_debugger_cmd, sizeof(_debugger_cmd), LIBTOOL_COMMAND " --mode execute %s %s", tool->exec, _edi_project_config->launch.path);
   else if (tool->arguments)
     snprintf(_debugger_cmd, sizeof(_debugger_cmd), "%s %s %s", tool->exec, tool->arguments, _edi_project_config->launch.path);
   else
     snprintf(_debugger_cmd, sizeof(_debugger_cmd), "%s %s", tool->exec, _edi_project_config->launch.path);

   elm_object_disabled_set(_button_int, EINA_FALSE);
   elm_object_disabled_set(_button_term, EINA_FALSE);
   elm_object_disabled_set(_button_quit, EINA_FALSE);
   elm_object_disabled_set(_button_start, EINA_TRUE);

   _edi_debugger_run(tool);
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

   timer = ecore_timer_add(1.0, _edi_debug_active_check_cb, NULL);
   (void) timer;

   elm_box_pack_end(parent, widget);
   elm_box_pack_end(parent, table);

   ecore_event_handler_add(EDI_EVENT_CONFIG_CHANGED, _edi_debugpanel_config_changed, NULL);
}
