/*
 * << Haru Free PDF Library 2.0.0 >> -- outline_demo.c
 *
 * Copyright (c) 1999-2006 Takeshi Kanno <takeshi_kanno@est.hi-ho.ne.jp>
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.
 * It is provided "as is" without express or implied warranty.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "hpdf.h"

jmp_buf env;

#ifdef HPDF_DLL
void  __stdcall
#else
void
#endif
error_handler  (HPDF_STATUS   error_no,
                HPDF_STATUS   detail_no,
                void         *user_data)
{
    (void) user_data; /* Not used */
    printf ("ERROR: error_no=%04X, detail_no=%u\n", (HPDF_UINT)error_no,
                (HPDF_UINT)detail_no);
    longjmp(env, 1);
}


void
print_page  (HPDF_Page   page,  int page_num)
{
    char buf[50];

    HPDF_Page_SetWidth (page, 800);
    HPDF_Page_SetHeight (page, 800);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, 30, 740);
#ifdef __WIN32__
    _snprintf(buf, 50, "Page:%d", page_num);
#else
    snprintf(buf, 50, "Page:%d", page_num);
#endif
    HPDF_Page_ShowText (page, buf);
    HPDF_Page_EndText (page);
}

int main(int argc, char **argv)
{
    (void) argc; /* Not used */
    HPDF_Doc  pdf;
    HPDF_Font font;
    HPDF_Page page[4];
    HPDF_Outline root;
    HPDF_Outline outline[4];
    HPDF_Destination dst;
    char fname[256];

    strcpy (fname, argv[0]);
    strcat (fname, ".pdf");

    pdf = HPDF_New (error_handler, NULL);
    if (!pdf) {
        printf ("error: cannot create PdfDoc object\n");
        return 1;
    }

    if (setjmp(env)) {
        HPDF_Free (pdf);
        return 1;
    }

    /* create default-font */
    font = HPDF_GetFont (pdf, "Helvetica", NULL);

    /* Set page mode to use outlines. */
    HPDF_SetPageMode(pdf, HPDF_PAGE_MODE_USE_OUTLINE);

    /* Add 3 pages to the document. */
    page[0] = HPDF_AddPage (pdf);
    HPDF_Page_SetFontAndSize (page[0], font, 30);
    print_page(page[0], 1);

    page[1] = HPDF_AddPage (pdf);
    HPDF_Page_SetFontAndSize (page[1], font, 30);
    print_page(page[1], 2);

    page[2] = HPDF_AddPage (pdf);
    HPDF_Page_SetFontAndSize (page[2], font, 30);
    print_page(page[2], 3);

    /* create outline root. */
    root = HPDF_CreateOutline (pdf, NULL, "OutlineRoot", NULL);
    HPDF_Outline_SetOpened (root, HPDF_TRUE);

    outline[0] = HPDF_CreateOutline (pdf, root, "page1", NULL);
    outline[1] = HPDF_CreateOutline (pdf, root, "page2", NULL);

    /* create outline with test which is ISO8859-2 encoding */
    outline[2] = HPDF_CreateOutline (pdf, root, "ISO8859-2 text гдежзий",
                    HPDF_GetEncoder (pdf, "ISO8859-2"));

    /* create destination objects on each pages
     * and link it to outline items.
     */
    dst = HPDF_Page_CreateDestination (page[0]);
    HPDF_Destination_SetXYZ(dst, 0, HPDF_Page_GetHeight(page[0]), 1);
    HPDF_Outline_SetDestination(outline[0], dst);
  //  HPDF_Catalog_SetOpenAction(dst);

    dst = HPDF_Page_CreateDestination (page[1]);
    HPDF_Destination_SetXYZ(dst, 0, HPDF_Page_GetHeight(page[1]), 1);
    HPDF_Outline_SetDestination(outline[1], dst);

    dst = HPDF_Page_CreateDestination (page[2]);
    HPDF_Destination_SetXYZ(dst, 0, HPDF_Page_GetHeight(page[2]), 1);
    HPDF_Outline_SetDestination(outline[2], dst);

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}
