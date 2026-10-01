/*
 * << Haru Free PDF Library 2.0.0 >> -- ext_gstate_demo.c
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
#include "grid_sheet.h"

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
draw_circles (HPDF_Page page, const char *description, HPDF_REAL x, HPDF_REAL y)
{
    HPDF_Page_SetLineWidth (page, 1.0f);
    HPDF_Page_SetRGBStroke (page, 0.0f, 0.0f, 0.0f);
    HPDF_Page_SetRGBFill (page, 1.0f, 0.0f, 0.0f);
    HPDF_Page_Circle (page, x + 40, y + 40, 40);
    HPDF_Page_ClosePathFillStroke (page);
    HPDF_Page_SetRGBFill (page, 0.0f, 1.0f, 0.0f);
    HPDF_Page_Circle (page, x + 100, y + 40, 40);
    HPDF_Page_ClosePathFillStroke (page);
    HPDF_Page_SetRGBFill (page, 0.0f, 0.0f, 1.0f);
    HPDF_Page_Circle (page, x + 70, y + 74.64, 40);
    HPDF_Page_ClosePathFillStroke (page);

    HPDF_Page_SetRGBFill (page, 0.0f, 0.0f, 0.0f);
    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, x + 0.0f, y + 130.0f, description);
    HPDF_Page_EndText (page);
}


int
main (int argc, char **argv)
{
    (void) argc; /* Not used */
    HPDF_Doc  pdf;
    HPDF_Page page;
    char fname[256];
    HPDF_Font hfont;
    HPDF_ExtGState gstate;
    const HPDF_REAL PAGE_WIDTH = 600;
    const HPDF_REAL PAGE_HEIGHT = 900;

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

    hfont = HPDF_GetFont (pdf, "Helvetica-Bold", NULL);

    /* add a new page object. */
    page = HPDF_AddPage (pdf);

    HPDF_Page_SetFontAndSize (page, hfont, 10);

    HPDF_Page_SetHeight (page, PAGE_HEIGHT);
    HPDF_Page_SetWidth (page, PAGE_WIDTH);

    /* normal */
    HPDF_Page_GSave (page);
    draw_circles (page, "normal", 40.0f, PAGE_HEIGHT - 170);
    HPDF_Page_GRestore (page);

    /* transparency (0.8) */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetAlphaFill (gstate, 0.8);
    HPDF_ExtGState_SetAlphaStroke (gstate, 0.8);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "alpha fill = 0.8", 230.0f, PAGE_HEIGHT - 170);
    HPDF_Page_GRestore (page);

    /* transparency (0.4) */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetAlphaFill (gstate, 0.4);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "alpha fill = 0.4", 420.0f, PAGE_HEIGHT - 170);
    HPDF_Page_GRestore (page);

    /* blend-mode=HPDF_BM_MULTIPLY */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_MULTIPLY);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_MULTIPLY", 40.0f, PAGE_HEIGHT - 340);
    HPDF_Page_GRestore (page);

    /* blend-mode=HPDF_BM_SCREEN */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_SCREEN);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_SCREEN", 230.0f, PAGE_HEIGHT - 340);
    HPDF_Page_GRestore (page);

    /* blend-mode=HPDF_BM_OVERLAY */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_OVERLAY);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_OVERLAY", 420.0f, PAGE_HEIGHT - 340);
    HPDF_Page_GRestore (page);

    /* blend-mode=HPDF_BM_DARKEN */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_DARKEN);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_DARKEN", 40.0f, PAGE_HEIGHT - 510);
    HPDF_Page_GRestore (page);

    /* blend-mode=HPDF_BM_LIGHTEN */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_LIGHTEN);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_LIGHTEN", 230.0f, PAGE_HEIGHT - 510);
    HPDF_Page_GRestore (page);

    /* blend-mode=HPDF_BM_COLOR_DODGE */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_COLOR_DODGE);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_COLOR_DODGE", 420.0f, PAGE_HEIGHT - 510);
    HPDF_Page_GRestore (page);


    /* blend-mode=HPDF_BM_COLOR_BUM */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_COLOR_BUM);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_COLOR_BUM", 40.0f, PAGE_HEIGHT - 680);
    HPDF_Page_GRestore (page);

    /* blend-mode=HPDF_BM_HARD_LIGHT */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_HARD_LIGHT);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_HARD_LIGHT", 230.0f, PAGE_HEIGHT - 680);
    HPDF_Page_GRestore (page);

    /* blend-mode=HPDF_BM_SOFT_LIGHT */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_SOFT_LIGHT);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_SOFT_LIGHT", 420.0f, PAGE_HEIGHT - 680);
    HPDF_Page_GRestore (page);

    /* blend-mode=HPDF_BM_DIFFERENCE */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_DIFFERENCE);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_DIFFERENCE", 40.0f, PAGE_HEIGHT - 850);
    HPDF_Page_GRestore (page);


    /* blend-mode=HPDF_BM_EXCLUSHON */
    HPDF_Page_GSave (page);
    gstate = HPDF_CreateExtGState (pdf);
    HPDF_ExtGState_SetBlendMode (gstate, HPDF_BM_EXCLUSHON);
    HPDF_Page_SetExtGState (page, gstate);
    draw_circles (page, "HPDF_BM_EXCLUSHON", 230.0f, PAGE_HEIGHT - 850);
    HPDF_Page_GRestore (page);


    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}

