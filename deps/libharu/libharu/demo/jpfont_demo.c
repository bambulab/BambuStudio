/*
 * << Haru Free PDF Library 2.0.0 >> -- jpfont_demo.c
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
    (void) argc; /* Not used */
    HPDF_Doc  pdf;
    char fname[256];
    HPDF_Font title_font;
    HPDF_Font detail_font[16];
    char samp_text[2048];
    HPDF_Outline root;
    HPDF_UINT i;
    const HPDF_UINT PAGE_HEIGHT = 210;

#ifdef __WIN32__
    FILE* f = fopen ("mbtext\\sjis.txt", "rb");
#else
    FILE* f = fopen ("mbtext/sjis.txt", "rb");
#endif

    if (!f) {
        printf ("error: cannot open 'mbtext/sjis.txt'\n");
        return 1;
    }

    fgets (samp_text, 2048, f);
    fclose (f);

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

    /* configure pdf-document to be compressed. */
    HPDF_SetCompressionMode (pdf, HPDF_COMP_ALL);

    /* declaration for using Japanese font, encoding. */
    HPDF_UseJPEncodings (pdf);
    HPDF_UseJPFonts (pdf);

    detail_font[0] = HPDF_GetFont (pdf, "MS-Mincho", "90ms-RKSJ-H");
    detail_font[1] = HPDF_GetFont (pdf, "MS-Mincho,Bold", "90ms-RKSJ-H");
    detail_font[2] = HPDF_GetFont (pdf, "MS-Mincho,Italic", "90ms-RKSJ-H");
    detail_font[3] = HPDF_GetFont (pdf, "MS-Mincho,BoldItalic", "90ms-RKSJ-H");
    detail_font[4] = HPDF_GetFont (pdf, "MS-PMincho", "90msp-RKSJ-H");
    detail_font[5] = HPDF_GetFont (pdf, "MS-PMincho,Bold", "90msp-RKSJ-H");
    detail_font[6] = HPDF_GetFont (pdf, "MS-PMincho,Italic", "90msp-RKSJ-H");
    detail_font[7] = HPDF_GetFont (pdf, "MS-PMincho,BoldItalic",
            "90msp-RKSJ-H");
    detail_font[8] = HPDF_GetFont (pdf, "MS-Gothic", "90ms-RKSJ-H");
    detail_font[9] = HPDF_GetFont (pdf, "MS-Gothic,Bold", "90ms-RKSJ-H");
    detail_font[10] = HPDF_GetFont (pdf, "MS-Gothic,Italic", "90ms-RKSJ-H");
    detail_font[11] = HPDF_GetFont (pdf, "MS-Gothic,BoldItalic", "90ms-RKSJ-H");
    detail_font[12] = HPDF_GetFont (pdf, "MS-PGothic", "90msp-RKSJ-H");
    detail_font[13] = HPDF_GetFont (pdf, "MS-PGothic,Bold", "90msp-RKSJ-H");
    detail_font[14] = HPDF_GetFont (pdf, "MS-PGothic,Italic", "90msp-RKSJ-H");
    detail_font[15] = HPDF_GetFont (pdf, "MS-PGothic,BoldItalic",
            "90msp-RKSJ-H");

    /* Set page mode to use outlines. */
    HPDF_SetPageMode(pdf, HPDF_PAGE_MODE_USE_OUTLINE);

    /* create outline root. */
    root = HPDF_CreateOutline (pdf, NULL, "JP font demo", NULL);
    HPDF_Outline_SetOpened (root, HPDF_TRUE);

    for (i = 0; i <= 15; i++) {
        HPDF_Page page;
        HPDF_Outline outline;
        HPDF_Destination dst;
        HPDF_REAL x_pos;
        HPDF_Point p;
        HPDF_UINT j;

        /* add a new page object. */
        page = HPDF_AddPage (pdf);

        /* create outline entry */
        outline = HPDF_CreateOutline (pdf, root,
                HPDF_Font_GetFontName (detail_font[i]), NULL);
        dst = HPDF_Page_CreateDestination (page);
        HPDF_Outline_SetDestination(outline, dst);

        title_font = HPDF_GetFont (pdf, "Helvetica", NULL);
        HPDF_Page_SetFontAndSize (page, title_font, 10);

        HPDF_Page_BeginText (page);

        /* move the position of the text to top of the page. */
        HPDF_Page_MoveTextPos(page, 10, 190);
        HPDF_Page_ShowText (page, HPDF_Font_GetFontName (detail_font[i]));

        HPDF_Page_SetFontAndSize (page, detail_font[i], 15);
        HPDF_Page_MoveTextPos (page, 10, -20);
        HPDF_Page_ShowText (page, "abcdefghijklmnopqrstuvwxyz");
        HPDF_Page_MoveTextPos (page, 0, -20);
        HPDF_Page_ShowText (page, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        HPDF_Page_MoveTextPos (page, 0, -20);
        HPDF_Page_ShowText (page, "1234567890");
        HPDF_Page_MoveTextPos (page, 0, -20);

        HPDF_Page_SetFontAndSize (page, detail_font[i], 10);
        HPDF_Page_ShowText (page, samp_text);
        HPDF_Page_MoveTextPos (page, 0, -18);

        HPDF_Page_SetFontAndSize (page, detail_font[i], 16);
        HPDF_Page_ShowText (page, samp_text);
        HPDF_Page_MoveTextPos (page, 0, -27);

        HPDF_Page_SetFontAndSize (page, detail_font[i], 23);
        HPDF_Page_ShowText (page, samp_text);
        HPDF_Page_MoveTextPos (page, 0, -36);

        HPDF_Page_SetFontAndSize (page, detail_font[i], 30);
        HPDF_Page_ShowText (page, samp_text);

        p = HPDF_Page_GetCurrentTextPos (page);

        /* finish to print text. */
        HPDF_Page_EndText (page);

        HPDF_Page_SetLineWidth (page, 0.5);

        x_pos = 20;
        for (j = 0; j <= strlen (samp_text) / 2; j++) {
            HPDF_Page_MoveTo (page, x_pos, p.y - 10);
            HPDF_Page_LineTo (page, x_pos, p.y - 12);
            HPDF_Page_Stroke (page);
            x_pos = x_pos + 30;
        }

        HPDF_Page_SetWidth (page, p.x + 20);
        HPDF_Page_SetHeight (page, PAGE_HEIGHT);

        HPDF_Page_MoveTo (page, 10, PAGE_HEIGHT - 25);
        HPDF_Page_LineTo (page, p.x + 10, PAGE_HEIGHT - 25);
        HPDF_Page_Stroke (page);

        HPDF_Page_MoveTo (page, 10, PAGE_HEIGHT - 85);
        HPDF_Page_LineTo (page, p.x + 10, PAGE_HEIGHT - 85);
        HPDF_Page_Stroke (page);

        HPDF_Page_MoveTo (page, 10, p.y - 12);
        HPDF_Page_LineTo (page, p.x + 10, p.y - 12);
        HPDF_Page_Stroke (page);
    }

    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}

