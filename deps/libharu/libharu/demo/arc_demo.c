/*
 * << Haru Free PDF Library 2.0.0 >> -- arc_demo.c
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

int
main (int argc, char **argv)
{
    (void) argc; /* Not used */
    HPDF_Doc  pdf;
    HPDF_Page page;
    char fname[256];
    HPDF_Point pos;

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

    /* add a new page object. */
    page = HPDF_AddPage (pdf);

    HPDF_Page_SetHeight (page, 220);
    HPDF_Page_SetWidth (page, 200);

    /* draw grid to the page */
    print_grid  (pdf, page);

    /* draw pie chart
     *
     *   A: 45% Red
     *   B: 25% Blue
     *   C: 15% green
     *   D: other yellow
     */

    /* A */
    HPDF_Page_SetRGBFill (page, 1.0, 0, 0);
    HPDF_Page_MoveTo (page, 100, 100);
    HPDF_Page_LineTo (page, 100, 180);
    HPDF_Page_Arc (page, 100, 100, 80, 0, 360 * 0.45);
    pos = HPDF_Page_GetCurrentPos (page);
    HPDF_Page_LineTo (page, 100, 100);
    HPDF_Page_Fill (page);

    /* B */
    HPDF_Page_SetRGBFill (page, 0, 0, 1.0);
    HPDF_Page_MoveTo (page, 100, 100);
    HPDF_Page_LineTo (page, pos.x, pos.y);
    HPDF_Page_Arc (page, 100, 100, 80, 360 * 0.45, 360 * 0.7);
    pos = HPDF_Page_GetCurrentPos (page);
    HPDF_Page_LineTo (page, 100, 100);
    HPDF_Page_Fill (page);

    /* C */
    HPDF_Page_SetRGBFill (page, 0, 1.0, 0);
    HPDF_Page_MoveTo (page, 100, 100);
    HPDF_Page_LineTo (page, pos.x, pos.y);
    HPDF_Page_Arc (page, 100, 100, 80, 360 * 0.7, 360 * 0.85);
    pos = HPDF_Page_GetCurrentPos (page);
    HPDF_Page_LineTo (page, 100, 100);
    HPDF_Page_Fill (page);

    /* D */
    HPDF_Page_SetRGBFill (page, 1.0, 1.0, 0);
    HPDF_Page_MoveTo (page, 100, 100);
    HPDF_Page_LineTo (page, pos.x, pos.y);
    HPDF_Page_Arc (page, 100, 100, 80, 360 * 0.85, 360);
    pos = HPDF_Page_GetCurrentPos (page);
    HPDF_Page_LineTo (page, 100, 100);
    HPDF_Page_Fill (page);

    /* draw center circle */
    HPDF_Page_SetGrayStroke (page, 0);
    HPDF_Page_SetGrayFill (page, 1);
    HPDF_Page_Circle (page, 100, 100, 30);
    HPDF_Page_Fill (page);

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}

