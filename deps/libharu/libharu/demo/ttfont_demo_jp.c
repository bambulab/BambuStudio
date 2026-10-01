/*
 * << Haru Free PDF Library 2.0.0 >> -- ttfont_demo_jp.c
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
    HPDF_Doc  pdf;
    char fname[256];
    HPDF_Page page;
    HPDF_Font title_font;
    HPDF_Font detail_font;
    HPDF_UINT page_height;
    HPDF_UINT page_width;
    HPDF_REAL pw;
    char SAMP_TXT[2048];
    const char *detail_font_name;
    FILE *f;

    if (argc < 2) {
        printf ("ttfont_jp_demo <ttf-font-filename> [-E]\n");
        printf ("ttfont_jp_demo <ttc-font-filename> <index> [-E]\n");
        return 1;
    }

#ifdef __WIN32__
    f = fopen ("mbtext\\sjis.txt", "rb");
#else
    f = fopen ("mbtext/sjis.txt", "rb");
#endif

    if (!f) {
        printf ("error: cannot open 'mbtext/sjis.txt'\n");
        return 1;
    }

    fgets (SAMP_TXT, 2048, f);
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

    /* declaration for using Japanese encoding. */
    HPDF_UseJPEncodings (pdf);

    HPDF_SetCompressionMode (pdf, HPDF_COMP_ALL);

    /* load ttc file */
    if (argc == 4 && strcmp (argv[3], "-E") == 0)
        detail_font_name = HPDF_LoadTTFontFromFile2 (pdf, argv[1],
                atoi (argv[2]), HPDF_TRUE);
    else if (argc == 3 && strcmp (argv[2], "-E") == 0)
        detail_font_name = HPDF_LoadTTFontFromFile (pdf, argv[1], HPDF_TRUE);
    else if (argc == 3)
        detail_font_name = HPDF_LoadTTFontFromFile2 (pdf, argv[1],
                atoi (argv[2]), HPDF_FALSE);
    else
        detail_font_name = HPDF_LoadTTFontFromFile (pdf, argv[1], HPDF_FALSE);

    /* add a new page object. */
    page = HPDF_AddPage (pdf);

    title_font = HPDF_GetFont (pdf, "Helvetica", NULL);

    detail_font = HPDF_GetFont (pdf, detail_font_name, "90msp-RKSJ-H");

    HPDF_Page_SetFontAndSize (page, title_font, 10);

    HPDF_Page_BeginText (page);

    /* move the position of the text to top of the page. */
    HPDF_Page_MoveTextPos(page, 10, 190);
    HPDF_Page_ShowText (page, detail_font_name);
    HPDF_Page_ShowText (page, " (");
    HPDF_Page_ShowText (page, HPDF_Font_GetEncodingName (detail_font));
    HPDF_Page_ShowText (page, ")");

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

    /* finish to print text. */
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

