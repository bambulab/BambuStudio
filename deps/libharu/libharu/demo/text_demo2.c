/*
 * << Alternative PDF Library 1.0.0 >> -- text_demo2.c
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
#include <math.h>
#include <setjmp.h>
#include "hpdf.h"
#include "grid_sheet.h"

jmp_buf env;

#ifdef HPDF_DLL
void __stdcall
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

int no = 0;

void
PrintText(HPDF_Page page)
{
    char buf[512];
    HPDF_Point pos = HPDF_Page_GetCurrentTextPos (page);

    no++;
    #ifdef __WIN32__
    _snprintf (buf, 512, ".[%d]%0.2f %0.2f", no, pos.x, pos.y);
    #else
    snprintf (buf, 512, ".[%d]%0.2f %0.2f", no, pos.x, pos.y);
    #endif
    HPDF_Page_ShowText(page, buf);
}


int
main (int argc, char **argv)
{
    (void) argc; /* Not used */
    HPDF_Doc  pdf;
    HPDF_Page page;
    char fname[256];
    HPDF_Font font;
    float angle1;
    float angle2;
    float rad1;
    float rad2;
    HPDF_Rect rect;

    const char* SAMP_TXT = "The quick brown fox jumps over the lazy dog. ";

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
    HPDF_Page_SetSize (page, HPDF_PAGE_SIZE_A5, HPDF_PAGE_PORTRAIT);

    print_grid  (pdf, page);

    font = HPDF_GetFont (pdf, "Helvetica", NULL);
    HPDF_Page_SetTextLeading (page, 20);

    /* text_rect method */

    /* HPDF_TALIGN_LEFT */
    rect.left = 25;
    rect.top = 545;
    rect.right = 200;
    rect.bottom = rect.top - 40;

    HPDF_Page_Rectangle (page, rect.left, rect.bottom, rect.right - rect.left,
                rect.top - rect.bottom);
    HPDF_Page_Stroke (page);

    HPDF_Page_BeginText (page);

    HPDF_Page_SetFontAndSize (page, font, 10);
    HPDF_Page_TextOut (page, rect.left, rect.top + 3, "HPDF_TALIGN_LEFT");

    HPDF_Page_SetFontAndSize (page, font, 13);
    HPDF_Page_TextRect (page, rect.left, rect.top, rect.right, rect.bottom,
                SAMP_TXT, HPDF_TALIGN_LEFT, NULL);

    HPDF_Page_EndText (page);

    /* HPDF_TALIGN_RIGTH */
    rect.left = 220;
    rect.right = 395;

    HPDF_Page_Rectangle (page, rect.left, rect.bottom, rect.right - rect.left,
                rect.top - rect.bottom);
    HPDF_Page_Stroke (page);

    HPDF_Page_BeginText (page);

    HPDF_Page_SetFontAndSize (page, font, 10);
    HPDF_Page_TextOut (page, rect.left, rect.top + 3, "HPDF_TALIGN_RIGTH");

    HPDF_Page_SetFontAndSize (page, font, 13);
    HPDF_Page_TextRect (page, rect.left, rect.top, rect.right, rect.bottom,
                SAMP_TXT, HPDF_TALIGN_RIGHT, NULL);

    HPDF_Page_EndText (page);

    /* HPDF_TALIGN_CENTER */
    rect.left = 25;
    rect.top = 475;
    rect.right = 200;
    rect.bottom = rect.top - 40;

    HPDF_Page_Rectangle (page, rect.left, rect.bottom, rect.right - rect.left,
                rect.top - rect.bottom);
    HPDF_Page_Stroke (page);

    HPDF_Page_BeginText (page);

    HPDF_Page_SetFontAndSize (page, font, 10);
    HPDF_Page_TextOut (page, rect.left, rect.top + 3, "HPDF_TALIGN_CENTER");

    HPDF_Page_SetFontAndSize (page, font, 13);
    HPDF_Page_TextRect (page, rect.left, rect.top, rect.right, rect.bottom,
                SAMP_TXT, HPDF_TALIGN_CENTER, NULL);

    HPDF_Page_EndText (page);

    /* HPDF_TALIGN_JUSTIFY */
    rect.left = 220;
    rect.right = 395;

    HPDF_Page_Rectangle (page, rect.left, rect.bottom, rect.right - rect.left,
                rect.top - rect.bottom);
    HPDF_Page_Stroke (page);

    HPDF_Page_BeginText (page);

    HPDF_Page_SetFontAndSize (page, font, 10);
    HPDF_Page_TextOut (page, rect.left, rect.top + 3, "HPDF_TALIGN_JUSTIFY");

    HPDF_Page_SetFontAndSize (page, font, 13);
    HPDF_Page_TextRect (page, rect.left, rect.top, rect.right, rect.bottom,
                SAMP_TXT, HPDF_TALIGN_JUSTIFY, NULL);

    HPDF_Page_EndText (page);



    /* Skewed coordinate system */
    HPDF_Page_GSave (page);

    angle1 = 5;
    angle2 = 10;
    rad1 = angle1 / 180 * 3.141592;
    rad2 = angle2 / 180 * 3.141592;

    HPDF_Page_Concat (page, 1, tan(rad1), tan(rad2), 1, 25, 350);
    rect.left = 0;
    rect.top = 40;
    rect.right = 175;
    rect.bottom = 0;

    HPDF_Page_Rectangle (page, rect.left, rect.bottom, rect.right - rect.left,
                rect.top - rect.bottom);
    HPDF_Page_Stroke (page);

    HPDF_Page_BeginText (page);

    HPDF_Page_SetFontAndSize (page, font, 10);
    HPDF_Page_TextOut (page, rect.left, rect.top + 3, "Skewed coordinate system");

    HPDF_Page_SetFontAndSize (page, font, 13);
    HPDF_Page_TextRect (page, rect.left, rect.top, rect.right, rect.bottom,
                SAMP_TXT, HPDF_TALIGN_LEFT, NULL);

    HPDF_Page_EndText (page);

    HPDF_Page_GRestore (page);


    /* Rotated coordinate system */
    HPDF_Page_GSave (page);

    angle1 = 5;
    rad1 = angle1 / 180 * 3.141592;

    HPDF_Page_Concat (page, cos(rad1), sin(rad1), -sin(rad1), cos(rad1), 220, 350);
    rect.left = 0;
    rect.top = 40;
    rect.right = 175;
    rect.bottom = 0;

    HPDF_Page_Rectangle (page, rect.left, rect.bottom, rect.right - rect.left,
                rect.top - rect.bottom);
    HPDF_Page_Stroke (page);

    HPDF_Page_BeginText (page);

    HPDF_Page_SetFontAndSize (page, font, 10);
    HPDF_Page_TextOut (page, rect.left, rect.top + 3, "Rotated coordinate system");

    HPDF_Page_SetFontAndSize (page, font, 13);
    HPDF_Page_TextRect (page, rect.left, rect.top, rect.right, rect.bottom,
                SAMP_TXT, HPDF_TALIGN_LEFT, NULL);

    HPDF_Page_EndText (page);

    HPDF_Page_GRestore (page);


    /* text along a circle */
    HPDF_Page_SetGrayStroke (page, 0);
    HPDF_Page_Circle (page, 210, 190, 145);
    HPDF_Page_Circle (page, 210, 190, 113);
    HPDF_Page_Stroke (page);

    angle1 = 360 / (strlen (SAMP_TXT));
    angle2 = 180;

    HPDF_Page_BeginText (page);
    font = HPDF_GetFont (pdf, "Courier-Bold", NULL);
    HPDF_Page_SetFontAndSize (page, font, 30);

    for (unsigned int i = 0; i < strlen (SAMP_TXT); i++) {
        char buf[2];
        float x;
        float y;

        rad1 = (angle2 - 90) / 180 * 3.141592;
        rad2 = angle2 / 180 * 3.141592;

        x = 210 + cos(rad2) * 122;
        y = 190 + sin(rad2) * 122;

        HPDF_Page_SetTextMatrix(page, cos(rad1), sin(rad1), -sin(rad1), cos(rad1), x, y);

        buf[0] = SAMP_TXT[i];
        buf[1] = 0;
        HPDF_Page_ShowText (page, buf);
        angle2 -= angle1;
    }

    HPDF_Page_EndText (page);

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}



