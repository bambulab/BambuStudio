/*
 * << Haru Free PDF Library 2.0.0 >> -- link_annotation.c
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
print_page  (HPDF_Page page, HPDF_Font font, int page_num)
{
    char buf[50];

    HPDF_Page_SetWidth (page, 200);
    HPDF_Page_SetHeight (page, 200);

    HPDF_Page_SetFontAndSize (page, font, 20);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, 50, 150);
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
    HPDF_Page index_page;
    HPDF_Page page[9];
    HPDF_Destination dst;
    char fname[256];
    HPDF_Rect rect;
    HPDF_Point tp;
    HPDF_Annotation annot;
    HPDF_UINT i;
    const char *uri = "http://libharu.org";

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

    /* create index page */
    index_page = HPDF_AddPage (pdf);
    HPDF_Page_SetWidth (index_page, 300);
    HPDF_Page_SetHeight (index_page, 220);

    /* Add 7 pages to the document. */
    for (i = 0; i < 7; i++) {
        page[i] = HPDF_AddPage (pdf);
        print_page(page[i], font, i + 1);
    }

    HPDF_Page_BeginText (index_page);
    HPDF_Page_SetFontAndSize (index_page, font, 10);
    HPDF_Page_MoveTextPos (index_page, 15, 200);
    HPDF_Page_ShowText (index_page, "Link Annotation Demo");
    HPDF_Page_EndText (index_page);

    /*
     * Create Link-Annotation object on index page.
     */
    HPDF_Page_BeginText(index_page);
    HPDF_Page_SetFontAndSize (index_page, font, 8);
    HPDF_Page_MoveTextPos (index_page, 20, 180);
    HPDF_Page_SetTextLeading (index_page, 23);

    /* page1 (HPDF_ANNOT_NO_HIGHTLIGHT) */
    tp = HPDF_Page_GetCurrentTextPos (index_page);

    HPDF_Page_ShowText (index_page, "Jump to Page1 (HilightMode=HPDF_ANNOT_NO_HIGHTLIGHT)");
    rect.left = tp.x - 4;
    rect.bottom = tp.y - 4;
    rect.right = HPDF_Page_GetCurrentTextPos (index_page).x + 4;
    rect.top = tp.y + 10;

    HPDF_Page_MoveToNextLine (index_page);

    dst = HPDF_Page_CreateDestination (page[0]);

    annot = HPDF_Page_CreateLinkAnnot (index_page, rect, dst);

    HPDF_LinkAnnot_SetHighlightMode (annot, HPDF_ANNOT_NO_HIGHTLIGHT);


    /* page2 (HPDF_ANNOT_INVERT_BOX) */
    tp = HPDF_Page_GetCurrentTextPos (index_page);

    HPDF_Page_ShowText (index_page, "Jump to Page2 (HilightMode=HPDF_ANNOT_INVERT_BOX)");
    rect.left = tp.x - 4;
    rect.bottom = tp.y - 4;
    rect.right = HPDF_Page_GetCurrentTextPos (index_page).x + 4;
    rect.top = tp.y + 10;

    HPDF_Page_MoveToNextLine (index_page);

    dst = HPDF_Page_CreateDestination (page[1]);

    annot = HPDF_Page_CreateLinkAnnot (index_page, rect, dst);

    HPDF_LinkAnnot_SetHighlightMode (annot, HPDF_ANNOT_INVERT_BOX);


    /* page3 (HPDF_ANNOT_INVERT_BORDER) */
    tp = HPDF_Page_GetCurrentTextPos (index_page);

    HPDF_Page_ShowText (index_page, "Jump to Page3 (HilightMode=HPDF_ANNOT_INVERT_BORDER)");
    rect.left = tp.x - 4;
    rect.bottom = tp.y - 4;
    rect.right = HPDF_Page_GetCurrentTextPos (index_page).x + 4;
    rect.top = tp.y + 10;

    HPDF_Page_MoveToNextLine (index_page);

    dst = HPDF_Page_CreateDestination (page[2]);

    annot = HPDF_Page_CreateLinkAnnot (index_page, rect, dst);

    HPDF_LinkAnnot_SetHighlightMode (annot, HPDF_ANNOT_INVERT_BORDER);


    /* page4 (HPDF_ANNOT_DOWN_APPEARANCE) */
    tp = HPDF_Page_GetCurrentTextPos (index_page);

    HPDF_Page_ShowText (index_page, "Jump to Page4 (HilightMode=HPDF_ANNOT_DOWN_APPEARANCE)");
    rect.left = tp.x - 4;
    rect.bottom = tp.y - 4;
    rect.right = HPDF_Page_GetCurrentTextPos (index_page).x + 4;
    rect.top = tp.y + 10;

    HPDF_Page_MoveToNextLine (index_page);

    dst = HPDF_Page_CreateDestination (page[3]);

    annot = HPDF_Page_CreateLinkAnnot (index_page, rect, dst);

    HPDF_LinkAnnot_SetHighlightMode (annot, HPDF_ANNOT_DOWN_APPEARANCE);


    /* page5 (dash border) */
    tp = HPDF_Page_GetCurrentTextPos (index_page);

    HPDF_Page_ShowText (index_page, "Jump to Page5 (dash border)");
    rect.left = tp.x - 4;
    rect.bottom = tp.y - 4;
    rect.right = HPDF_Page_GetCurrentTextPos (index_page).x + 4;
    rect.top = tp.y + 10;

    HPDF_Page_MoveToNextLine (index_page);

    dst = HPDF_Page_CreateDestination (page[4]);

    annot = HPDF_Page_CreateLinkAnnot (index_page, rect, dst);

    HPDF_LinkAnnot_SetBorderStyle (annot, 1, 3, 2);


    /* page6 (no border) */
    tp = HPDF_Page_GetCurrentTextPos (index_page);

    HPDF_Page_ShowText (index_page, "Jump to Page6 (no border)");
    rect.left = tp.x - 4;
    rect.bottom = tp.y - 4;
    rect.right = HPDF_Page_GetCurrentTextPos (index_page).x + 4;
    rect.top = tp.y + 10;

    HPDF_Page_MoveToNextLine (index_page);

    dst = HPDF_Page_CreateDestination (page[5]);

    annot = HPDF_Page_CreateLinkAnnot (index_page, rect, dst);

    HPDF_LinkAnnot_SetBorderStyle (annot, 0, 0, 0);


    /* page7 (bold border) */
    tp = HPDF_Page_GetCurrentTextPos (index_page);

    HPDF_Page_ShowText (index_page, "Jump to Page7 (bold border)");
    rect.left = tp.x - 4;
    rect.bottom = tp.y - 4;
    rect.right = HPDF_Page_GetCurrentTextPos (index_page).x + 4;
    rect.top = tp.y + 10;

    HPDF_Page_MoveToNextLine (index_page);

    dst = HPDF_Page_CreateDestination (page[6]);

    annot = HPDF_Page_CreateLinkAnnot (index_page, rect, dst);

    HPDF_LinkAnnot_SetBorderStyle (annot, 2, 0, 0);


    /* URI link */
    tp = HPDF_Page_GetCurrentTextPos (index_page);

    HPDF_Page_ShowText (index_page, "URI (");
    HPDF_Page_ShowText (index_page, uri);
    HPDF_Page_ShowText (index_page, ")");

    rect.left = tp.x - 4;
    rect.bottom = tp.y - 4;
    rect.right = HPDF_Page_GetCurrentTextPos (index_page).x + 4;
    rect.top = tp.y + 10;

    HPDF_Page_CreateURILinkAnnot (index_page, rect, uri);

    HPDF_Page_EndText (index_page);

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}
