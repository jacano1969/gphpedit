/* This file is part of gPHPEdit, a GNOME2 PHP Editor.

   Copyright (C) 2003, 2004, 2005 Andy Jeffries <andy at gphpedit.org>
   Copyright (C) 2009 Anoop John <anoop dot john at zyxware.com>
   Copyright (C) 2009 José Rostagno (for vijona.com.ar) 

   For more information or to find the latest release, visit our 
   website at http://www.gphpedit.org/

   gPHPEdit is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   gPHPEdit is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with gPHPEdit. If not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <gtk/gtk.h>
#include "classbrowser_backend.h"
#include "main_window_callbacks.h"
#include "gvfs_utils.h"
//#define DEBUGCLASSBROWSER 

/* object signal enumeration */
enum {
	DONE_REFRESH,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

/*
* classbrowser_backend private struct
*/
struct Classbrowser_BackendDetails
{
  GSList *functionlist;
  GTree *php_variables_tree;
  GTree *php_class_tree;
  guint identifierid;
};

typedef struct
{
  gchar *filename;
  gboolean accessible;
  GTimeVal modified_time;
}
ClassBrowserFile;

typedef struct
{
  gchar *varname;
  gchar *functionname;
  gchar *filename;
  gboolean remove;
  guint identifierid;
}
ClassBrowserVar;

/*
 * classbrowser_backend_get_type
 * register Classbrowser_Backend type and returns a new GType
*/

static gpointer parent_class;
static void               classbrowser_backend_finalize         (GObject                *object);
static void               classbrowser_backend_init             (gpointer                object,
							       gpointer                klass);
static void classbrowser_backend_class_init (Classbrowser_BackendClass *klass);
static void classbrowser_backend_dispose (GObject *gobject);

#define CLASSBROWSER_BACKEND_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object),\
					    CLASSBROWSER_BACKEND_TYPE,\
					    Classbrowser_BackendDetails))

void classbrowser_remove_dead_wood(Classbrowser_Backend *classback);
void do_parse_file(Classbrowser_Backend *classback, Editor *editor);
void free_function_list_item (gpointer data, gpointer user_data);
#ifdef HAVE_CTAGS_EXUBERANT
void call_ctags(Classbrowser_Backend *classback, gchar *filename);
#endif

GType
classbrowser_backend_get_type (void)
{
    static GType our_type = 0;
    
    if (!our_type) {
        static const GTypeInfo our_info =
        {
            sizeof (Classbrowser_BackendClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) classbrowser_backend_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof (Classbrowser_Backend),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) classbrowser_backend_init,
        };

        our_type = g_type_register_static (G_TYPE_OBJECT, "Classbrowser_Backend",
                                           &our_info, 0);
  }
    
    return our_type;
}

void
classbrowser_backend_class_init (Classbrowser_BackendClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
  parent_class = g_type_class_peek_parent (klass);
	object_class->finalize = classbrowser_backend_finalize;
  object_class->dispose = classbrowser_backend_dispose;

/*
if load is ok return TRUE. if load isn't complete return FALSE
*/
	signals[DONE_REFRESH] =
		g_signal_new ("done_refresh",
		              G_TYPE_FROM_CLASS (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (Classbrowser_BackendClass, done_refresh),
		              NULL, NULL,
		               g_cclosure_marshal_VOID__BOOLEAN ,
		               G_TYPE_NONE, 1, G_TYPE_BOOLEAN, NULL);

	g_type_class_add_private (klass, sizeof (Classbrowser_BackendDetails));
}

void
classbrowser_backend_init (gpointer object, gpointer klass)
{
//	Classbrowser_BackendDetails *classbackdet;
//	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(object);
}

/*
* disposes the Gobject
*/
void classbrowser_backend_dispose (GObject *object)
{
  Classbrowser_Backend *classback = CLASSBROWSER_BACKEND(object);
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  /* free object resources*/
  if (classbackdet->functionlist){
   g_slist_foreach (classbackdet->functionlist, free_function_list_item, NULL);
   g_slist_free (classbackdet->functionlist);
  }
  if (classbackdet->php_variables_tree) g_tree_destroy (classbackdet->php_variables_tree);
  if (classbackdet->php_class_tree) g_tree_destroy (classbackdet->php_class_tree);
  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
classbrowser_backend_finalize (GObject *object)
{
//  Classbrowser_Backend *classback = CLASSBROWSER_BACKEND(object);
//  Classbrowser_BackendDetails *classbackdet;
//	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  
  //free class data

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

Classbrowser_Backend *classbrowser_backend_new (void)
{
	Classbrowser_Backend *classback;
  classback = g_object_new (CLASSBROWSER_BACKEND_TYPE, NULL);
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  classbackdet->identifierid = 0;

	return classback; /* return new object */
}

void free_php_variables_tree_item (gpointer data) 
{
  ClassBrowserVar *var=(ClassBrowserVar *)data;
  if (var->varname) g_free(var->varname);
  if (var->functionname) g_free(var->functionname);
  if (var->filename) g_free(var->filename);
  if (var) g_slice_free(ClassBrowserVar, var);
}

void free_php_class_tree_item (gpointer data) 
{
  ClassBrowserClass *class = (ClassBrowserClass *) data;
  if (class->filename) g_free(class->filename);
  if (class->classname) g_free(class->classname);
  if (class) g_slice_free(ClassBrowserClass, class);
}


void free_function_list_item (gpointer data, gpointer user_data)
{
ClassBrowserFunction *function = (ClassBrowserFunction *) data;
  g_free(function->filename);
  g_free(function->functionname);
  if (function->paramlist) {
    g_free(function->paramlist);
  }
  if (function->classname) {
    g_free(function->classname);
  }
  g_slice_free(ClassBrowserFunction,function);
}
void add_global_var(Classbrowser_BackendDetails *classbackdet, const gchar *var_name)
{
  ClassBrowserVar *var;
    var = g_slice_new(ClassBrowserVar);
    var->varname = g_strdup(var_name);
    var->functionname = NULL; /* NULL for global variables*/
    var->filename = NULL; /*NULL FOR PHP GLOBAL VARIABLES*/
    var->remove = FALSE;
    var->identifierid = classbackdet->identifierid++;

    g_tree_insert (classbackdet->php_variables_tree, g_strdup(var_name), var); /* key = variables name value var struct */

}

/* release resources used by classbrowser */
gboolean classbrowser_php_class_set_remove_item (gpointer key, gpointer value, gpointer data){
  ClassBrowserClass *class=(ClassBrowserClass *)value;
  if (class) {
    class->remove = TRUE;
  }
  return FALSE;	
}

void list_php_files_open (gpointer data, gpointer user_data){
  do_parse_file(user_data, data);
}
/*
* do_parse_file
* parse an editor
* if editor is PHP uses our internal code
* otherwise use CTAGS EXUBERANT if it's avariable.
* //FIXME: CTAGS EXUBERANT don't support CSS files
*/
void do_parse_file(Classbrowser_Backend *classback, Editor *editor){
    if (editor && GTK_IS_SCINTILLA(editor->scintilla)) {
    if (editor->is_untitled) return;
      #ifdef CLASSBROWSER
      g_print("classbrowser found:%s\n",editor->filename->str);
      #endif
    while (gtk_events_pending()) gtk_main_iteration(); /* update ui */
    #ifdef DEBUGCLASSBROWSER
      g_print("Parsing %s\n", editor->filename->str);
    #endif
      if (is_php_file_from_filename(editor->filename->str)) {
        classbrowser_parse_file(classback, editor->filename->str); 
#ifdef HAVE_CTAGS_EXUBERANT
      } else {
        /* CTAGS don't support CSS files */
        if (!is_css_file(editor->filename->str)) call_ctags(classback, editor->filename->str);
#endif
      }
    }  
}

void classbrowser_backend_start_update(Classbrowser_BackendDetails *classbackdet)
{
  GSList *li;
  ClassBrowserFunction *function;

  for(li = classbackdet->functionlist; li!= NULL; li = g_slist_next(li)) {
    function = li->data;
    if (function) {
      function->remove = TRUE;
    }
  }
  if (!classbackdet->php_class_tree){
     /* create new tree */
     classbackdet->php_class_tree= g_tree_new_full((GCompareDataFunc) g_utf8_collate, NULL, g_free, free_php_class_tree_item);
  } else {
     g_tree_foreach (classbackdet->php_class_tree, classbrowser_php_class_set_remove_item,NULL);
  }
}

//FIXME: this function can be optimized by not requesting to reparse files on tab change
//when the parse only selected tab is set - Anoop
void classbrowser_backend_update(Classbrowser_Backend *classback, GSList *editor_list, gboolean only_current_file)
{
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  if (!classbackdet->php_variables_tree){
     /* create new tree */
     classbackdet->php_variables_tree=g_tree_new_full((GCompareDataFunc) g_strcmp0, NULL, g_free, free_php_variables_tree_item);

     /*add php global vars*/
     add_global_var(classbackdet, "$GLOBALS");
     add_global_var(classbackdet, "$HTTP_POST_VARS");
     add_global_var(classbackdet, "$HTTP_RAW_POST_DATA");
     add_global_var(classbackdet, "$http_response_header");
     add_global_var(classbackdet, "$this");
     add_global_var(classbackdet, "$_COOKIE");
     add_global_var(classbackdet, "$_POST");
     add_global_var(classbackdet, "$_REQUEST");
     add_global_var(classbackdet, "$_SERVER");
     add_global_var(classbackdet, "$_SESSION");
     add_global_var(classbackdet, "$_GET");
     add_global_var(classbackdet, "$_FILES");
     add_global_var(classbackdet, "$_ENV");
     add_global_var(classbackdet, "__CLASS__");
     add_global_var(classbackdet, "__DIR__");
     add_global_var(classbackdet, "__FILE__");
     add_global_var(classbackdet, "__FUNCTION__");
     add_global_var(classbackdet, "__METHOD__");
     add_global_var(classbackdet, "__NAMESPACE__");
  }
  
  classbrowser_backend_start_update(classbackdet);
  if (only_current_file){
    //add only if there is a current editor
    if (main_window.current_editor) {
      do_parse_file(classback, main_window.current_editor);
    }
  } else {
    if (editor_list){
    g_slist_foreach (editor_list, list_php_files_open, classback); 
    }
  }
  classbrowser_remove_dead_wood(classback);
  g_signal_emit (G_OBJECT (classback), signals[DONE_REFRESH], 0, TRUE); /* emit process and update UI */
}

void classbrowser_classlist_remove(Classbrowser_Backend *classback, ClassBrowserClass *class)
{
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);

  gchar *keyname=g_strdup_printf("%s%s",class->classname,class->filename);
  g_tree_remove (classbackdet->php_class_tree,keyname);
  g_free(keyname);
}

gboolean classbrowser_remove_class(gpointer key, gpointer value, gpointer data){
  ClassBrowserClass *class= (ClassBrowserClass *)value;
      if (class) {
      if (class->remove) {
        classbrowser_classlist_remove(data, class);
      }
    }
  return FALSE;
}

void classbrowser_backend_functionlist_free(Classbrowser_Backend *classback, ClassBrowserFunction *function)
{
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);

  g_free(function->filename);
  g_free(function->functionname);
  if (function->paramlist) {
    g_free(function->paramlist);
  }
  if (function->classname) {
    g_free(function->classname);
  }
  classbackdet->functionlist = g_slist_remove(classbackdet->functionlist, function);
  g_slice_free(ClassBrowserFunction,function);
}

void classbrowser_remove_dead_wood(Classbrowser_Backend *classback)
{
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);

  GSList *orig;
  GSList *li;
  ClassBrowserFunction *function;

  orig = g_slist_copy(classbackdet->functionlist);
  for(li = orig; li!= NULL; li = g_slist_next(li)) {
    function = li->data;
    if (function) {
      if (function->remove) {
        classbrowser_backend_functionlist_free(classback, function);
      }
    }
  }
  g_slist_free(orig);
  g_tree_foreach (classbackdet->php_class_tree, classbrowser_remove_class, classback);
}

void classbrowser_varlist_add(Classbrowser_Backend *classback, gchar *varname, gchar *funcname, gchar *filename)
{
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  ClassBrowserVar *var;
  var=g_tree_lookup (classbackdet->php_variables_tree, varname);
  if (var){
    var->remove = FALSE;
  } else {
    var = g_slice_new0(ClassBrowserVar);
    var->varname = g_strdup(varname);
    if (funcname) {
      var->functionname = g_strdup(funcname);
    }
    var->filename = g_strdup(filename);
    var->remove = FALSE;
    var->identifierid = classbackdet->identifierid++;

    g_tree_insert (classbackdet->php_variables_tree, g_strdup(varname), var); /* key =variables name value var struct */

    #ifdef DEBUGCLASSBROWSER
      g_print("Filename: %s\n", filename);
    #endif
  }
}

void classbrowser_classlist_add(Classbrowser_Backend *classback, gchar *classname, gchar *filename, gint line_number,gint file_type)
{
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);

  ClassBrowserClass *class;
  gchar *keyname=g_strdup_printf("%s%s",classname,filename);
  class=g_tree_lookup (classbackdet->php_class_tree, keyname);
  if ((class)){
    class->line_number = line_number;
    class->remove= FALSE;
    g_free(keyname);
  } else {
    class = g_slice_new0(ClassBrowserClass);
    class->classname = g_strdup(classname);
    class->filename = g_strdup(filename);
    class->line_number = line_number;
    class->remove = FALSE;
    class->identifierid = classbackdet->identifierid++;
    class->file_type=file_type;
    g_tree_insert (classbackdet->php_class_tree,keyname,class);
  }
}

#ifdef HAVE_CTAGS_EXUBERANT
gchar *get_ctags_token(gchar *text,gint *advancing){
  int i;
  int k=0;
  gchar *name;
  gchar *part = text;
  name=part;
  for (i=0;i<strlen(text);i++){
    /* process until get a space*/
    if (*(part+i)==' '){
      while (*(part+i+k)==' ') k++; /*count spaces*/
      break;
    }
  }
  name=g_malloc0(i+1);
  strncpy(name,part,i);
  *advancing=i+k; /* skip spaces*/
  return name;
}
//FIXME:: only COBOL support for now
void call_ctags(Classbrowser_Backend *classback, gchar *filename){
  if (!filename){
    g_print("skip\n");
    return;
  }
  gboolean result;
  gchar *stdout;
  gint exit_status;
  GError *error=NULL;
  gchar *stdouterr;
  gchar *path=filename_get_path(filename);
  gchar *command_line=g_strdup_printf("ctags -x '%s'",path);
  result = g_spawn_command_line_sync (command_line, &stdout, &stdouterr, &exit_status, &error);
  g_free(command_line);
  g_free(path);
  if (result) {
  // g_print("ctags:%s ->(%s)\n",stdout,stdouterr);

  gchar *copy;
  gchar *token;
  gchar *name;
  gchar *type;
  gchar *line;
  copy = stdout;
    while ((token = strtok(copy, "\n"))) {
        gint ad=0;
        name=get_ctags_token(token,&ad);
//        g_print("name:%s ",name);
        token+=ad;
        type=get_ctags_token(token,&ad);
//        g_print("type:%s ",type);
        token+=ad;
        line=get_ctags_token(token,&ad);
//        g_print("line:%s\n",line);
        if (is_cobol_file(filename))
            process_cobol_word(classback, name, filename, type, line);
        g_free(name);
        g_free(line);
        g_free(type);
        copy = NULL;
      }
    //we have all functions in the same GTree and we distinguish by filetype (PHP,COBOL,C/C++,PERL,PYTHON,ect).
    g_free(stdouterr);
    g_free(stdout);
  }
}
#endif

ClassBrowserFunction *classbrowser_functionlist_find(Classbrowser_BackendDetails *classbackdet, gchar *funcname, gchar *param_list, gchar *filename, gchar *classname)
{
  GSList *li;
  ClassBrowserFunction *function;
  gboolean found;

  for(li = classbackdet->functionlist; li!= NULL; li = g_slist_next(li)) {
    function = li->data;
    if (function) {
      found = TRUE;
      if (g_strcmp0(function->functionname, funcname)==0 &&
              g_strcmp0(function->filename, filename)==0 &&
              g_strcmp0(function->paramlist, param_list)==0 &&
              g_strcmp0(function->classname, classname)==0) {
        return function;
      }
    }
  }

  return NULL;
}

void classbrowser_functionlist_add(Classbrowser_Backend *classback, gchar *classname, gchar *funcname, gchar *filename, gint file_type, guint line_number, gchar *param_list)
{
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);

  ClassBrowserClass *class;
  ClassBrowserFunction *function;
  if ((function = classbrowser_functionlist_find(classbackdet, funcname, param_list, filename, classname))) {
    function->line_number = line_number;
    function->remove = FALSE;
  } else {
    function = g_slice_new0(ClassBrowserFunction);
    function->functionname = g_strdup(funcname);
    if (param_list) {
      function->paramlist = g_strdup(param_list);
    }
    function->filename = g_strdup(filename);
    function->line_number = line_number;
    function->remove = FALSE;
    function->identifierid = classbackdet->identifierid++;
    function->file_type = file_type;
    gchar *keyname=g_strdup_printf("%s%s",classname,filename);
    if (classname && (class = g_tree_lookup (classbackdet->php_class_tree,keyname))){
      function->class_id = class->identifierid;
      function->classname = g_strdup(classname);
    }
    g_free(keyname);
    classbackdet->functionlist = g_slist_append(classbackdet->functionlist, function);
  }
}

GSList *classbrowser_backend_get_function_list(Classbrowser_Backend *classback){
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  return classbackdet->functionlist; 
}

GTree *classbrowser_backend_get_class_list(Classbrowser_Backend *classback){
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  return classbackdet->php_class_tree; 
}



gboolean classbrowser_file_in_list_find(GSList *list, gchar *file)
{
  GSList *list_walk;
  gchar *data;

  for(list_walk = list; list_walk!= NULL; list_walk = g_slist_next(list_walk)) {
    data = list_walk->data;
    if (g_utf8_collate(data, file)==0) {
      return TRUE;
    }
  }
  return FALSE;
}

/* FILE label section */

/**
 * Compare two filenames and find the length of the part of the
 * directory names that match each other. Eg: passing ./home/src/a.php
 * and ./home/b.php will return 7 i.e. the length of the common
 * part of the directory names.
 */
guint get_longest_matching_length(gchar *filename1, gchar *filename2)
{
  gchar *base1, *base1_alloc;
  gchar *base2, *base2_alloc;
  guint length;

  //Store the pointers so as to be freed in the end.
  base1 = g_path_get_dirname(filename1);
  base1_alloc = base1;
  base2 = g_path_get_dirname(filename2);
  base2_alloc = base2;

  length = 0;
  //Check only if both base paths are not ".".
  if (strcmp(base2_alloc, ".")!=0 && strcmp(base2_alloc, ".")!=0) {
    //Increment count and move along the characters in both paths
    //while they are equal and compare till the shorter of the two.
    while (*base1 && *base2 && (*base1 == *base2)) {
      base1++;
      base2++;
      length++;
    }
  }

  g_free(base1_alloc);
  g_free(base2_alloc);

  return length;
}

/**
 * 
 */
GString *get_differing_part(GSList *filenames, gchar *file_requested)
{
  GSList *temp_list;
  gchar buffer[1024];
  guint longest_match;
  guint match;

  longest_match = 9999;

  // Loop through and find the length of the shortest matching basepath
  // Seems to miss the first one - if that's not required, change to temp_list = filenames
  for(temp_list = filenames; temp_list!= NULL; temp_list = g_slist_next(temp_list)) {
    match = get_longest_matching_length(temp_list->data, file_requested);
    //debug("String: %s\nString: %s\nMatch: %d", temp_list->data, file_requested, match);
    if (match < longest_match) {
      longest_match = match;
    }
  }
  //debug("Match: %d", longest_match);
  if (longest_match!=9999) {
    if (*(file_requested + longest_match) == '/') {
      strcpy(buffer, (file_requested + longest_match+1));
    }
    else {
      strcpy(buffer, (file_requested + longest_match));
    }
  }
  else {
    strcpy(buffer, file_requested);
  }

  return g_string_new(buffer);
}

GString *get_differing_part_editor(Editor *editor)
{
  gchar *cwd;
  GSList *list_editors;
  GSList *list_filenames;
  Editor *data;
  gchar *str;
  GString *result;

  if (editor == NULL) 
    return NULL;
  
  cwd = g_get_current_dir();

  list_filenames = NULL;
  list_filenames = g_slist_append(list_filenames, cwd);

  for(list_editors = editors; list_editors!= NULL; list_editors = g_slist_next(list_editors)) {
    data = list_editors->data;
    if (data->type == TAB_FILE) {
      str = ((Editor *)data)->filename->str;
      list_filenames = g_slist_append(list_filenames, str);
    }
  }

  result = get_differing_part(list_filenames, editor->filename->str);
  g_free(cwd);
  return result;
}

/*
* classbrowser_backend_get_selected_label
* return a new GString with new label text
*/
GString *classbrowser_backend_get_selected_label(Classbrowser_Backend *classback, gchar *filename, gint line)
{
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  GSList *filenames;
  GSList *function_walk;
  GString *new_label;
  ClassBrowserFunction *function;
  gchar *func_filename;
  gint num_files;

  filenames = NULL;
  num_files = 0;
  for(function_walk = classbackdet->functionlist; function_walk!= NULL; function_walk = g_slist_next(function_walk)) {
    num_files++;
    function = function_walk->data;
    if (function) {
      func_filename = function->filename;
      // g_slist_find and g_slist_index don't seem to work, always return NULL or -1 respec.
      if (!classbrowser_file_in_list_find(filenames, func_filename)) {
        filenames = g_slist_prepend(filenames, func_filename);
      }
    }
  }
  if(num_files < 2) {
    gchar *basename=filename_get_basename(filename);
    new_label = g_string_new(basename);
    g_free(basename);
  }
  else {
    new_label = get_differing_part(filenames, filename);
  }
  g_slist_free(filenames);
  return new_label;
}

static gboolean make_class_completion_string (gpointer key, gpointer value, gpointer data){
  GString *completion_result= (GString *) data;
  ClassBrowserClass *class;
  class=(ClassBrowserClass *)value;
        if (!completion_result) {
        completion_result = g_string_new(g_strchug(class->classname));
        completion_result = g_string_append(completion_result, "?4"); /* add corresponding image*/
        } else {
        completion_result = g_string_append(completion_result, " ");
        completion_result = g_string_append(completion_result, g_strchug(class->classname));
        completion_result = g_string_append(completion_result, "?4"); /* add corresponding image*/
        }
  return FALSE;
}

GString *classbrowser_backend_get_autocomplete_php_classes_string(Classbrowser_Backend *classback){
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  GString *completion_result = NULL;
  g_tree_foreach (classbackdet->php_class_tree, make_class_completion_string, completion_result);
  return completion_result;
}

typedef struct {
 gchar *prefix;
 GString *completion_result;
} var_find;

static gboolean make_completion_string (gpointer key, gpointer value, gpointer data){
  var_find *search_data = (var_find *)data;
  ClassBrowserVar *var;
  var=(ClassBrowserVar *)value;
  if (g_str_has_prefix(key,search_data->prefix)){
        if (!search_data->completion_result) {
        search_data->completion_result = g_string_new(key);
        search_data->completion_result = g_string_append(search_data->completion_result, "?3");
        } else {
        search_data->completion_result = g_string_append(search_data->completion_result, " ");
        search_data->completion_result = g_string_append(search_data->completion_result, key);
        search_data->completion_result = g_string_append(search_data->completion_result, "?3"); /* add corresponding image*/
        }
  }
  return FALSE;
}

void classbrowser_backend_autocomplete_php_variables(Classbrowser_Backend *classback, GtkWidget *scintilla, gint wordStart, gint wordEnd){
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  gchar *buffer = NULL;
  gint length;
  buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), wordStart, wordEnd, &length);
#ifdef DEBUGCLASSBROWSER
  g_print("var autoc:%s\n",buffer);
#endif
  var_find *search_data= g_slice_new(var_find);
  search_data->prefix=buffer;
  search_data->completion_result = NULL;
  g_tree_foreach (classbackdet->php_variables_tree, make_completion_string,search_data);
  g_free(buffer);
  if (search_data->completion_result){
    gtk_scintilla_autoc_show(GTK_SCINTILLA(scintilla), wordEnd-wordStart, search_data->completion_result->str);
    g_string_free(search_data->completion_result,TRUE); /*release resources*/
  }
  g_slice_free(var_find, search_data);  
}

GString *get_member_function_completion_list(Classbrowser_BackendDetails *classbackdet, GtkWidget *scintilla, gint wordStart, gint wordEnd)
{
  gchar *buffer = NULL;
  GSList *li;
  GList *li2;
  ClassBrowserFunction *function;
  GList* member_functions = NULL;
  GList* sorted_member_functions = NULL;
  GString *result = NULL;
  gint length;
  gchar *function_name;

  buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), wordStart, wordEnd, &length);
  for(li = classbackdet->functionlist; li!= NULL; li = g_slist_next(li)) {
    function = li->data;
    if (function) {
      if ((g_str_has_prefix(function->functionname, buffer) || (wordStart==wordEnd)) && function->file_type==TAB_PHP ) {
        member_functions = g_list_append(member_functions, function->functionname);
      }
    }
  }

  sorted_member_functions = g_list_sort(member_functions, (GCompareFunc) g_utf8_collate);
  member_functions = sorted_member_functions;

  for(li2 = member_functions; li2!= NULL; li2 = g_list_next(li2)) {
    function_name = li2->data;
    if (!result) {
      result = g_string_new(function_name);
      result = g_string_append(result, "?1");
    }
    else {
      result = g_string_append(result, " ");
      result = g_string_append(result, function_name);
      result = g_string_append(result, "?1");
    }
  }

  result = g_string_append(result, " ");
  g_free(buffer);
  return result;
}


void classbrowser_backend_autocomplete_member_function(Classbrowser_Backend *classback, GtkWidget *scintilla, gint wordStart, gint wordEnd)
{
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  GString *list;

  list = get_member_function_completion_list(classbackdet, scintilla, wordStart, wordEnd);

  if (list) {
    gtk_scintilla_autoc_show(GTK_SCINTILLA(scintilla), wordEnd-wordStart, list->str);
    g_string_free(list, FALSE);
  }
}

gchar *classbrowser_backend_custom_function_calltip(Classbrowser_Backend *classback, gchar *function_name){
/*FIXME::two functions diferent classes same name =bad calltip */
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  GSList *li;
  ClassBrowserFunction *function;
  gchar *calltip=NULL;
  for(li = classbackdet->functionlist; li!= NULL; li = g_slist_next(li)) {
    function = li->data;
    if (function) {
      if (g_utf8_collate(function->functionname, function_name)==0 && function->file_type==TAB_PHP) {
          calltip=g_strdup_printf("%s (%s)",function->functionname,function->paramlist);
          break;
      }
    }
  }
  return calltip;
}

gchar *classbrowser_backend_add_custom_autocompletion(Classbrowser_Backend *classback, gchar *prefix, GSList *list){
  Classbrowser_BackendDetails *classbackdet;
	classbackdet = CLASSBROWSER_BACKEND_GET_PRIVATE(classback);
  GSList *li;
  GList *li2;
  ClassBrowserFunction *function;
  GString *result=NULL;
  GList* member_functions = NULL;
  GList* sorted_member_functions = NULL;
  gchar *function_name;
  for(li = classbackdet->functionlist; li!= NULL; li = g_slist_next(li)) {
    function = li->data;
    if (function) {
      if ((g_str_has_prefix(function->functionname, prefix) && function->file_type==TAB_PHP)) {
        member_functions = g_list_prepend(member_functions, function->functionname);
      }
    }
  }
  /* add functions */
  for(li = list; li!= NULL; li = g_slist_next(li)) {
    function = li->data;
    if (function) {
        member_functions = g_list_prepend(member_functions, function);
    }
  }
  sorted_member_functions = g_list_sort(member_functions, (GCompareFunc) g_utf8_collate);
  member_functions = sorted_member_functions;

  for(li2 = member_functions; li2!= NULL; li2 = g_list_next(li2)) {
    function_name = li2->data;
    if (!result) {
      result = g_string_new(function_name);
      if (!g_str_has_suffix(function_name,"?2"))
          result = g_string_append(result, "?1");
    }
    else {
      result = g_string_append(result, " ");
      result = g_string_append(result, function_name);
      if (!g_str_has_suffix(function_name,"?2") && !g_str_has_suffix(function_name,"?3"))
          result = g_string_append(result, "?1");
    }
  }
  if (result){
    result = g_string_append(result, " ");
    return result->str;
  } else {
    return NULL;
  }
}