#ifndef EDI_CONTENT_H_
# define EDI_CONTENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "mainview/edi_mainview_item.h"

/**
 * @file
 * @brief These routines are used for managing various content.
 */

/**
 * @brief Managing the creation of alternative content within EDI.
 * @defgroup Content
 *
 * @{
 *
 * Adding alternative editor types, for example images or diff widgets.
 *
 */

/**
 * Create an object for displaying image content.
 *
 * @param parent the panel into which the image widget will be loaded.
 * @param item the item describing the image file to be loaded in the canvas.
 *
 * @return an Evas_Object containing the image.
 *
 * @ingroup Content
 */
Evas_Object *edi_content_image_add(Evas_Object *parent, Edi_Mainview_Item *item);

/**
 * Create an object for diff content.
 *
 * @param parent the panel into which the diff widget will be loaded.
 * @param item the item describing the diff file to be loaded.
 *
 * @return an Evas_Object containing diff widget.
 *
 * @ingroup Content
 */
Evas_Object *edi_content_diff_add(Evas_Object *parent, Edi_Mainview_Item *item);

/**
 * @}
 */


#ifdef __cplusplus
}
#endif

#endif /* EDI_CONTENT_H_ */
