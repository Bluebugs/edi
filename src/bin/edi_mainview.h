#ifndef EDI_MAINVIEW_H_
# define EDI_MAINVIEW_H_

#include <Elementary.h>
#include <Evas.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for managing the main area of the Edi interface.
 */
 
 /**
 * @typedef Edi_Mainview_Item
 * An item being displayed in the mainview.
 */
typedef struct _Edi_Mainview_Item Edi_Mainview_Item;

/**
 * @struct _Edi_Mainview_Item
 * An item being displayed in the mainview.
 */
struct _Edi_Mainview_Item
{
   const char *path; /**< The path of the file in this mainview item */

   Elm_Object_Item *tab; /**< The tab object connected to this view */
   Elm_Object_Item *view; /**< The naviframe item that contains the view for this item */
   Evas_Object *win; /**< The window for the item if it's displayed in a seperate window */

   /* Private */

   /* Add new members here. */
};

/**
 * @brief UI management functions.
 * @defgroup UI
 *
 * @{
 *
 * Initialisation and management of the main view UI
 *
 */

/**
 * Initialize a new Edi main view and add it to the parent panel.
 *
 * @param parent The parent into which the panel will be loaded.
 * @param win The window the panel will be using
 *
 * @ingroup UI
 */
EAPI void edi_mainview_add(Evas_Object *parent, Evas_Object *win);

/**
 * @}
 *
 * 
 * @brief Content management functions.
 * @defgroup Content
 *
 * @{
 *
 * Opening and managing content within the view.
 *
 */

/**
 * Open the file at path for editing using the type specified.
 * Supported types are "text" and "image".
 *
 * @param path The absolute path of the file to open.
 * @param type The requested type to use when opening the file or NULL for auto-detect
 *
 * @ingroup Content
 */
EAPI void edi_mainview_open_path(const char *path, const char *type);

/**
 * Open the file at path for editing in a new window using the type specified.
 * Supported types are "text" and "image".
 * If the path is already open it will be moved to a new window.
 *
 * @param path The absolute path of the file to open.
 * @param type The requested type to use when opening the file or NULL for auto-detect
 *
 * @ingroup Content
 */
EAPI void edi_mainview_open_window_path(const char *path, const char *type);

/**
 * Save the current file.
 *
 * @ingroup Content
 */
EAPI void edi_mainview_save();

/**
 * Move the current tab to a new window.
 *
 * @ingroup Content
 */
EAPI void edi_mainview_new_window();

/**
 * Close the current file.
 *
 * @ingroup Content
 */
EAPI void edi_mainview_close();

/**
 * Cut the current selection into the clipboard.
 *
 * @ingroup Content
 */
EAPI void edi_mainview_cut();

/**
 * Copy the current selection into the clipboard.
 *
 * @ingroup Content
 */
EAPI void edi_mainview_copy();

/**
 * Paste the current clipboard contents at the current cursor position.
 *
 * @ingroup Content
 */
EAPI void edi_mainview_paste();

/**
 * Search the current view's contents.
 *
 * @ingroup Content
 */
EAPI void edi_mainview_search();

/**
 * @}
 *
 *
 * @brief Tab management functions.
 * @defgroup Tabs
 *
 * @{
 *
 * Manipulating the open files within the application.
 *
 */

/**
 * Select the passed item in the mainview UI.
 * By definition this will already be an open file as the Edi_Mainview_Item will
 * only exist for an open item.
 * If this is an external window it will raise that instead of selecting a tab.
 *
 * @ingroup Tabs
 */
EAPI void edi_mainview_item_select(Edi_Mainview_Item *item);

/**
 * Select the previous open tab.
 * Previous means the next tab left, if there is one.
 *
 * @ingroup Tabs
 */
EAPI void edi_mainview_item_prev();

/**
 * Select the next open tab.
 * Next means the next tab to the right, if there is one.
 *
 * @ingroup Tabs
 */
EAPI void edi_mainview_item_next();

/**
 * @}
 *
 */

#ifdef __cplusplus
}
#endif

#endif /* EDI_MAINVIEW_H_ */
