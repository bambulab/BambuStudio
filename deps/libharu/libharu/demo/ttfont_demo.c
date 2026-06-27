/*
 * << Haru Free PDF Library 2.0.0 >> -- ttfont_demo.c
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
error_handler (HPDF_STATUS   error_no,
               HPDF_STATUS   detail_no,
               void         *user_data)
{
    (void) user_data; /* Not used */
    printf ("ERROR: error_no=%04X, detail_no=%u\n", (HPDF_UINT)error_no,
                (HPDF_UINT)detail_no);
    longjmp(env, 1);
}

int main (int argc, char **argv)
{
    const char* SAMP_TXT = "The quick brown fox jumps over the lazy dog.";

    HPDF_Doc  pdf;
    char fname[256];
    HPDF_Page page;
    HPDF_Font title_font;
    HPDF_Font detail_font;
    const char *detail_font_name;
    HPDF_BOOL embed;
    HPDF_REAL page_height;
    HPDF_REAL page_width;
    HPDF_REAL pw;

    if (argc < 2) {
        printf("usage: ttfont_demo [path to font file] "
                "-E(embedding font).\n");
        return 1;
    }

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

    /* Add a new page object. */
    page = HPDF_AddPage (pdf);

    title_font = HPDF_GetFont (pdf, "Helvetica", NULL);

    if (argc > 2 && memcmp(argv[2], "-E", 2) == 0)
        embed = HPDF_TRUE;
    else
        embed = HPDF_FALSE;

    detail_font_name = HPDF_LoadTTFontFromFile (pdf, argv[1], embed);

    detail_font = HPDF_GetFont (pdf, detail_font_name, NULL);

    HPDF_Page_SetFontAndSize (page, title_font, 10);

    HPDF_Page_BeginText (page);

    /* Move the position of the text to top of the page. */
    HPDF_Page_MoveTextPos(page, 10, 190);
    HPDF_Page_ShowText (page, detail_font_name);

    if (embed)
        HPDF_Page_ShowText (page, "(Embedded Subset)");

    HPDF_Page_SetFontAndSize (page, detail_font, 15);
    HPDF_Page_MoveTextPos (page, 10, -20);
    HPDF_Page_ShowText (page, "abcdefghijklmnopqrstuvwxyz");
    HPDF_Page_MoveTextPos (page, 0, -20);
    HPDF_Page_ShowText (page, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    HPDF_Page_MoveTextPos (page, 0, -20);
    HPDF_Page_ShowText (page, "1234567890");
    HPDF_Page_MoveTextPos (page, 0, -20);

    HPDF_Page_SetFontAndSize (page, detail_font, 10);
    HPDF_Page_ShowText (page, SAMP_TXT);
    HPDF_Page_MoveTextPos (page, 0, -18);

    HPDF_Page_SetFontAndSize (page, detail_font, 16);
    HPDF_Page_ShowText (page, SAMP_TXT);
    HPDF_Page_MoveTextPos (page, 0, -27);

    HPDF_Page_SetFontAndSize (page, detail_font, 23);
    HPDF_Page_ShowText (page, SAMP_TXT);
    HPDF_Page_MoveTextPos (page, 0, -36);

    HPDF_Page_SetFontAndSize (page, detail_font, 30);
    HPDF_Page_ShowText (page, SAMP_TXT);
    HPDF_Page_MoveTextPos (page, 0, -36);

    pw = HPDF_Page_TextWidth (page, SAMP_TXT);
    page_height = 210;
    page_width = pw + 40;

    HPDF_Page_SetWidth (page, page_width);
    HPDF_Page_SetHeight (page, page_height);

    /* Finish to print text. */
    HPDF_Page_EndText (page);

    HPDF_Page_SetLineWidth (page, 0.5);

    HPDF_Page_MoveTo (page, 10, page_height - 25);
    HPDF_Page_LineTo (page, page_width - 10, page_height - 25);
    HPDF_Page_Stroke (page);

    HPDF_Page_MoveTo (page, 10, page_height - 85);
    HPDF_Page_LineTo (page, page_width - 10, page_height - 85);
    HPDF_Page_Stroke (page);

    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}

