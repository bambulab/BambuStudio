/*
 * << Haru Free PDF Library 2.0.6 >> -- slideshow_demo.c
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
print_page  (HPDF_Page  page, const char *caption, HPDF_Font font, 
    HPDF_TransitionStyle style, HPDF_Page prev, HPDF_Page next)
{
    float r = rand() / (RAND_MAX + 1.0);
    float g = rand() / (RAND_MAX + 1.0);
    float b = rand() / (RAND_MAX + 1.0);
    HPDF_Rect rect;
    HPDF_Destination dst;
    HPDF_Annotation annot;

    HPDF_Page_SetWidth (page, 800);
    HPDF_Page_SetHeight (page, 600);

    HPDF_Page_SetRGBFill (page, r, g, b);

    HPDF_Page_Rectangle (page, 0, 0, 800, 600);
    HPDF_Page_Fill (page);

    HPDF_Page_SetRGBFill (page, 1.0f - r, 1.0f - g, 1.0f - b);

    HPDF_Page_SetFontAndSize (page, font, 30);

    HPDF_Page_BeginText (page);
    HPDF_Page_SetTextMatrix (page, 0.8f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);   
    HPDF_Page_TextOut (page, 50, 530, caption);

    HPDF_Page_SetTextMatrix (page, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);   
    HPDF_Page_SetFontAndSize (page, font, 20);
    HPDF_Page_TextOut (page, 55, 300, 
            "Type \"Ctrl+L\" in order to return from full screen mode.");
    HPDF_Page_EndText (page);

    HPDF_Page_SetSlideShow (page, style, 5.0, 1.0);

    HPDF_Page_SetFontAndSize (page, font, 20);

    if (next) {
        HPDF_Page_BeginText (page);
        HPDF_Page_TextOut (page, 680, 50, "Next=>");
        HPDF_Page_EndText (page);

        rect.left = 680;
        rect.right = 750;
        rect.top = 70;
        rect.bottom = 50;
        dst = HPDF_Page_CreateDestination (next);
        HPDF_Destination_SetFit(dst);
        annot = HPDF_Page_CreateLinkAnnot (page, rect, dst);
        HPDF_LinkAnnot_SetBorderStyle (annot, 0, 0, 0);
        HPDF_LinkAnnot_SetHighlightMode (annot, HPDF_ANNOT_INVERT_BOX);
    } 

    if (prev) {
        HPDF_Page_BeginText (page);
        HPDF_Page_TextOut (page, 50, 50, "<=Prev");
        HPDF_Page_EndText (page);

        rect.left = 50;
        rect.right = 110;
        rect.top = 70;
        rect.bottom = 50;
        dst = HPDF_Page_CreateDestination (prev);
        HPDF_Destination_SetFit(dst);
        annot = HPDF_Page_CreateLinkAnnot (page, rect, dst);
        HPDF_LinkAnnot_SetBorderStyle (annot, 0, 0, 0);
        HPDF_LinkAnnot_SetHighlightMode (annot, HPDF_ANNOT_INVERT_BOX);
    }
        

}

int main(int argc, char **argv)
{
    (void) argc; /* Not used */
    HPDF_Doc  pdf;
    HPDF_Font font;
    HPDF_Page page[17];
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
    font = HPDF_GetFont (pdf, "Courier", NULL);

    /* Add 17 pages to the document. */
    page[0] = HPDF_AddPage (pdf);
    page[1] = HPDF_AddPage (pdf);
    page[2] = HPDF_AddPage (pdf);
    page[3] = HPDF_AddPage (pdf);
    page[4] = HPDF_AddPage (pdf);
    page[5] = HPDF_AddPage (pdf);
    page[6] = HPDF_AddPage (pdf);
    page[7] = HPDF_AddPage (pdf);
    page[8] = HPDF_AddPage (pdf);
    page[9] = HPDF_AddPage (pdf);
    page[10] = HPDF_AddPage (pdf);
    page[11] = HPDF_AddPage (pdf);
    page[12] = HPDF_AddPage (pdf);
    page[13] = HPDF_AddPage (pdf);
    page[14] = HPDF_AddPage (pdf);
    page[15] = HPDF_AddPage (pdf);
    page[16] = HPDF_AddPage (pdf);

    print_page(page[0], "HPDF_TS_WIPE_RIGHT", font, 
			HPDF_TS_WIPE_RIGHT, NULL, page[1]);
    print_page(page[1], "HPDF_TS_WIPE_UP", font, 
			HPDF_TS_WIPE_UP, page[0], page[2]);
    print_page(page[2], "HPDF_TS_WIPE_LEFT", font, 
			HPDF_TS_WIPE_LEFT, page[1], page[3]);
    print_page(page[3], "HPDF_TS_WIPE_DOWN", font, 
			HPDF_TS_WIPE_DOWN, page[2], page[4]);
    print_page(page[4], "HPDF_TS_BARN_DOORS_HORIZONTAL_OUT", font, 
			HPDF_TS_BARN_DOORS_HORIZONTAL_OUT, page[3], page[5]);
    print_page(page[5], "HPDF_TS_BARN_DOORS_HORIZONTAL_IN", font, 
			HPDF_TS_BARN_DOORS_HORIZONTAL_IN, page[4], page[6]);
    print_page(page[6], "HPDF_TS_BARN_DOORS_VERTICAL_OUT", font, 
			HPDF_TS_BARN_DOORS_VERTICAL_OUT, page[5], page[7]);
    print_page(page[7], "HPDF_TS_BARN_DOORS_VERTICAL_IN", font, 
			HPDF_TS_BARN_DOORS_VERTICAL_IN, page[6], page[8]);
    print_page(page[8], "HPDF_TS_BOX_OUT", font, 
			HPDF_TS_BOX_OUT, page[7], page[9]);
    print_page(page[9], "HPDF_TS_BOX_IN", font, 
			HPDF_TS_BOX_IN, page[8], page[10]);
    print_page(page[10], "HPDF_TS_BLINDS_HORIZONTAL", font, 
			HPDF_TS_BLINDS_HORIZONTAL, page[9], page[11]);
    print_page(page[11], "HPDF_TS_BLINDS_VERTICAL", font, 
			HPDF_TS_BLINDS_VERTICAL, page[10], page[12]);
    print_page(page[12], "HPDF_TS_DISSOLVE", font, 
			HPDF_TS_DISSOLVE, page[11], page[13]);
    print_page(page[13], "HPDF_TS_GLITTER_RIGHT", font, 
			HPDF_TS_GLITTER_RIGHT, page[12], page[14]);
    print_page(page[14], "HPDF_TS_GLITTER_DOWN", font, 
			HPDF_TS_GLITTER_DOWN, page[13], page[15]);
    print_page(page[15], "HPDF_TS_GLITTER_TOP_LEFT_TO_BOTTOM_RIGHT", font, 
			HPDF_TS_GLITTER_TOP_LEFT_TO_BOTTOM_RIGHT, page[14], page[16]);
    print_page(page[16], "HPDF_TS_REPLACE", font, 
			HPDF_TS_REPLACE, page[15], NULL);


    HPDF_SetPageMode (pdf, HPDF_PAGE_MODE_FULL_SCREEN);

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}
