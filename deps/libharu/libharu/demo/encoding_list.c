/*
 * << Haru Free PDF Library 2.0.0 >> -- encoding_list.c
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

static const int PAGE_WIDTH = 420;
static const int PAGE_HEIGHT = 400;
static const int CELL_WIDTH = 20;
static const int CELL_HEIGHT = 20;

void
draw_graph (HPDF_Page   page);


void
draw_fonts (HPDF_Page   page);


void
draw_graph (HPDF_Page   page)
{
    char buf[50];
    int i;

    /* Draw 16 X 15 cells */

    /* Draw vertical lines. */
    HPDF_Page_SetLineWidth (page, 0.5);

    for (i = 0; i <= 17; i++) {
        int x = i * CELL_WIDTH + 40;

        HPDF_Page_MoveTo (page, x, PAGE_HEIGHT - 60);
        HPDF_Page_LineTo (page, x, 40);
        HPDF_Page_Stroke (page);

        if (i > 0 && i <= 16) {
            HPDF_Page_BeginText (page);
            HPDF_Page_MoveTextPos (page, x + 5, PAGE_HEIGHT - 75);
#ifdef __WIN32__
            _snprintf(buf, 5, "%X", i - 1);
#else
            snprintf(buf, 5, "%X", i - 1);
#endif
            HPDF_Page_ShowText (page, buf);
            HPDF_Page_EndText (page);
        }
    }

    /* Draw horizontal lines. */
    for (i = 0; i <= 15; i++) {
       int y = i * CELL_HEIGHT + 40;

        HPDF_Page_MoveTo (page, 40, y);
        HPDF_Page_LineTo (page, PAGE_WIDTH - 40, y);
        HPDF_Page_Stroke (page);

        if (i < 14) {
            HPDF_Page_BeginText (page);
            HPDF_Page_MoveTextPos (page, 45, y + 5);
#ifdef __WIN32__
            _snprintf(buf, 5, "%X", 15 - i);
#else
            snprintf(buf, 5, "%X", 15 - i);
#endif
            HPDF_Page_ShowText (page, buf);
            HPDF_Page_EndText (page);
        }
    }
}


void
draw_fonts (HPDF_Page   page)
{
    int i;
    int j;

    HPDF_Page_BeginText (page);

    /* Draw all character from 0x20 to 0xFF to the canvas. */
    for (i = 1; i < 17; i++) {
        for (j = 1; j < 17; j++) {
            unsigned char buf[2];
            int y = PAGE_HEIGHT - 55 - ((i - 1) * CELL_HEIGHT);
            int x = j * CELL_WIDTH + 50;

            buf[1] = 0x00;

            buf[0] = (i - 1) * 16 + (j - 1);
            if (buf[0] >= 32) {
                double d;

                d  = x - HPDF_Page_TextWidth (page, (char*)buf) / 2;
                HPDF_Page_TextOut (page, d, y, (char*)buf);

            }
        }
    }

    HPDF_Page_EndText (page);
}


int main (int argc, char **argv)
{
    (void) argc; /* Not used */
    HPDF_Doc  pdf;
    char fname[256];
    HPDF_Font font;
    const char *font_name;
    int i = 0;
    HPDF_Outline root;

    const char *encodings[] = {
            "StandardEncoding",
            "MacRomanEncoding",
            "WinAnsiEncoding",
            "ISO8859-2",
            "ISO8859-3",
            "ISO8859-4",
            "ISO8859-5",
            "ISO8859-9",
            "ISO8859-10",
            "ISO8859-13",
            "ISO8859-14",
            "ISO8859-15",
            "ISO8859-16",
            "CP1250",
            "CP1251",
            "CP1252",
            "CP1254",
            "CP1257",
            "KOI8-R",
            "Symbol-Set",
            "ZapfDingbats-Set",
            NULL
    };

    pdf = HPDF_NewEx (error_handler, NULL, NULL, 0, NULL);
    if (!pdf) {
        printf ("error: cannot create PdfDoc object\n");
        return 1;
    }

    if (setjmp(env)) {
        HPDF_Free (pdf);
        return 1;
    }

    strcpy (fname, argv[0]);
    strcat (fname, ".pdf");

    /* set compression mode */
    HPDF_SetCompressionMode (pdf, HPDF_COMP_ALL);

    /* Set page mode to use outlines. */
    HPDF_SetPageMode(pdf, HPDF_PAGE_MODE_USE_OUTLINE);

    /* get default font */
    font = HPDF_GetFont (pdf, "Helvetica", NULL);

    /* load font object */
    #ifdef __WIN32__
    font_name = HPDF_LoadType1FontFromFile (pdf, "type1\\a010013l.afm",
            "type1\\a010013l.pfb");
    #else
    font_name = HPDF_LoadType1FontFromFile (pdf, "type1/a010013l.afm",
            "type1/a010013l.pfb");
    #endif

    /* create outline root. */
    root = HPDF_CreateOutline (pdf, NULL, "Encoding list", NULL);
    HPDF_Outline_SetOpened (root, HPDF_TRUE);

    while (encodings[i]) {
        HPDF_Page page = HPDF_AddPage (pdf);
        HPDF_Outline outline;
        HPDF_Destination dst;
        HPDF_Font font2;

        HPDF_Page_SetWidth (page, PAGE_WIDTH);
        HPDF_Page_SetHeight (page, PAGE_HEIGHT);

        outline = HPDF_CreateOutline (pdf, root, encodings[i], NULL);
        dst = HPDF_Page_CreateDestination (page);
        HPDF_Destination_SetXYZ(dst, 0, HPDF_Page_GetHeight(page), 1);
        /* HPDF_Destination_SetFitB(dst); */
        HPDF_Outline_SetDestination(outline, dst);

        HPDF_Page_SetFontAndSize (page, font, 15);
        draw_graph (page);

        HPDF_Page_BeginText (page);
        HPDF_Page_SetFontAndSize (page, font, 20);
        HPDF_Page_MoveTextPos (page, 40, PAGE_HEIGHT - 50);
        HPDF_Page_ShowText (page, encodings[i]);
        HPDF_Page_ShowText (page, " Encoding");
        HPDF_Page_EndText (page);

        if (strcmp (encodings[i], "Symbol-Set") == 0)
            font2 = HPDF_GetFont (pdf, "Symbol", NULL);
        else if (strcmp (encodings[i], "ZapfDingbats-Set") == 0)
            font2 = HPDF_GetFont (pdf, "ZapfDingbats", NULL);
        else
            font2 = HPDF_GetFont (pdf, font_name, encodings[i]);

        HPDF_Page_SetFontAndSize (page, font2, 14);
        draw_fonts (page);

        i++;
    }

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}
