/*
 * << Haru Free PDF Library 2.0.0 >> -- text_demo.c
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

void
show_stripe_pattern  (HPDF_Page   page,
                      HPDF_REAL   x,
                      HPDF_REAL   y)
{
    HPDF_UINT iy = 0;

    while (iy < 50) {
        HPDF_Page_SetRGBStroke (page, 0.0, 0.0, 0.5);
        HPDF_Page_SetLineWidth (page, 1);
        HPDF_Page_MoveTo (page, x, y + iy);
        HPDF_Page_LineTo (page, x + HPDF_Page_TextWidth (page, "ABCabc123"),
                    y + iy);
        HPDF_Page_Stroke (page);
        iy += 3;
    }

    HPDF_Page_SetLineWidth (page, 2.5);
}


void
show_description  (HPDF_Page          page,
                   HPDF_REAL          x,
                   HPDF_REAL          y,
                   const char   *text)
{
    float fsize = HPDF_Page_GetCurrentFontSize (page);
    HPDF_Font font = HPDF_Page_GetCurrentFont (page);
    HPDF_RGBColor c = HPDF_Page_GetRGBFill (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_SetRGBFill (page, 0, 0, 0);
    HPDF_Page_SetTextRenderingMode (page, HPDF_FILL);
    HPDF_Page_SetFontAndSize (page, font, 10);
    HPDF_Page_TextOut (page, x, y - 12, text);
    HPDF_Page_EndText (page);

    HPDF_Page_SetFontAndSize (page, font, fsize);
    HPDF_Page_SetRGBFill (page, c.r, c.g, c.b);
}


int main (int argc, char **argv)
{
    (void) argc; /* Not used */
    const char *page_title = "Text Demo";

    HPDF_Doc  pdf;
    HPDF_Font font;
    HPDF_Page page;
    char fname[256];

    const char* samp_text = "abcdefgABCDEFG123!#$%&+-@?";
    const char* samp_text2 = "The quick brown fox jumps over the lazy dog.";
    float tw;
    float fsize;
    int i;
    int len;

    float angle1;
    float angle2;
    float rad1;
    float rad2;

    float ypos;

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

    /* set compression mode */
    HPDF_SetCompressionMode (pdf, HPDF_COMP_ALL);

    /* create default-font */
    font = HPDF_GetFont (pdf, "Helvetica", NULL);

    /* add a new page object. */
    page = HPDF_AddPage (pdf);

    /* draw grid to the page */
    print_grid  (pdf, page);

    /* print the lines of the page.
    HPDF_Page_SetLineWidth (page, 1);
    HPDF_Page_Rectangle (page, 50, 50, HPDF_Page_GetWidth(page) - 100,
                HPDF_Page_GetHeight (page) - 110);
    HPDF_Page_Stroke (page);
    */

    /* print the title of the page (with positioning center). */
    HPDF_Page_SetFontAndSize (page, font, 24);
    tw = HPDF_Page_TextWidth (page, page_title);
    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, (HPDF_Page_GetWidth(page) - tw) / 2,
                HPDF_Page_GetHeight (page) - 50, page_title);
    HPDF_Page_EndText (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, 60, HPDF_Page_GetHeight(page) - 60);

    /*
     * font size
     */
    fsize = 8;
    while (fsize < 60) {
        char buf[50];
        int len;

        /* set style and size of font. */
        HPDF_Page_SetFontAndSize(page, font, fsize);

        /* set the position of the text. */
        HPDF_Page_MoveTextPos (page, 0, -5 - fsize);

        /* measure the number of characters which included in the page. */
        strcpy(buf, samp_text);
        len = HPDF_Page_MeasureText (page, samp_text,
                        HPDF_Page_GetWidth(page) - 120, HPDF_FALSE, NULL);

        /* truncate the text. */
        buf[len] = 0x00;

        HPDF_Page_ShowText (page, buf);

        /* print the description. */
        HPDF_Page_MoveTextPos (page, 0, -10);
        HPDF_Page_SetFontAndSize(page, font, 8);
        #ifdef __WIN32__
        _snprintf(buf, 50, "Fontsize=%.0f", fsize);
        #else
        snprintf(buf, 50, "Fontsize=%.0f", fsize);
        #endif
        HPDF_Page_ShowText (page, buf);

        fsize *= 1.5;
    }

    /*
     * font color
     */
    HPDF_Page_SetFontAndSize(page, font, 8);
    HPDF_Page_MoveTextPos (page, 0, -30);
    HPDF_Page_ShowText (page, "Font color");

    HPDF_Page_SetFontAndSize (page, font, 18);
    HPDF_Page_MoveTextPos (page, 0, -20);
    len = strlen (samp_text);
    for (i = 0; i < len; i++) {
        char buf[2];
        float r = (float)i / (float)len;
        float g = 1 - ((float)i / (float)len);
        buf[0] = samp_text[i];
        buf[1] = 0x00;

        HPDF_Page_SetRGBFill (page, r, g, 0.0);
        HPDF_Page_ShowText (page, buf);
    }
    HPDF_Page_MoveTextPos (page, 0, -25);

    for (i = 0; i < len; i++) {
        char buf[2];
        float r = (float)i / (float)len;
        float b = 1 - ((float)i / (float)len);
        buf[0] = samp_text[i];
        buf[1] = 0x00;

        HPDF_Page_SetRGBFill (page, r, 0.0, b);
        HPDF_Page_ShowText (page, buf);
    }
    HPDF_Page_MoveTextPos (page, 0, -25);

    for (i = 0; i < len; i++) {
        char buf[2];
        float b = (float)i / (float)len;
        float g = 1 - ((float)i / (float)len);
        buf[0] = samp_text[i];
        buf[1] = 0x00;

        HPDF_Page_SetRGBFill (page, 0.0, g, b);
        HPDF_Page_ShowText (page, buf);
    }

    HPDF_Page_EndText (page);

    ypos = 450;

    /*
     * Font rendering mode
     */
    HPDF_Page_SetFontAndSize(page, font, 32);
    HPDF_Page_SetRGBFill (page, 0.5, 0.5, 0.0);
    HPDF_Page_SetLineWidth (page, 1.5);

     /* PDF_FILL */
    show_description (page,  60, ypos,
                "RenderingMode=PDF_FILL");
    HPDF_Page_SetTextRenderingMode (page, HPDF_FILL);
    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, 60, ypos, "ABCabc123");
    HPDF_Page_EndText (page);

    /* PDF_STROKE */
    show_description (page, 60, ypos - 50,
                "RenderingMode=PDF_STROKE");
    HPDF_Page_SetTextRenderingMode (page, HPDF_STROKE);
    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, 60, ypos - 50, "ABCabc123");
    HPDF_Page_EndText (page);

    /* PDF_FILL_THEN_STROKE */
    show_description (page, 60, ypos - 100,
                "RenderingMode=PDF_FILL_THEN_STROKE");
    HPDF_Page_SetTextRenderingMode (page, HPDF_FILL_THEN_STROKE);
    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, 60, ypos - 100, "ABCabc123");
    HPDF_Page_EndText (page);

    /* PDF_FILL_CLIPPING */
    show_description (page, 60, ypos - 150,
                "RenderingMode=PDF_FILL_CLIPPING");
    HPDF_Page_GSave (page);
    HPDF_Page_SetTextRenderingMode (page, HPDF_FILL_CLIPPING);
    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, 60, ypos - 150, "ABCabc123");
    HPDF_Page_EndText (page);
    show_stripe_pattern (page, 60, ypos - 150);
    HPDF_Page_GRestore (page);

    /* PDF_STROKE_CLIPPING */
    show_description (page, 60, ypos - 200,
                "RenderingMode=PDF_STROKE_CLIPPING");
    HPDF_Page_GSave (page);
    HPDF_Page_SetTextRenderingMode (page, HPDF_STROKE_CLIPPING);
    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, 60, ypos - 200, "ABCabc123");
    HPDF_Page_EndText (page);
    show_stripe_pattern (page, 60, ypos - 200);
    HPDF_Page_GRestore (page);

    /* PDF_FILL_STROKE_CLIPPING */
    show_description (page, 60, ypos - 250,
                "RenderingMode=PDF_FILL_STROKE_CLIPPING");
    HPDF_Page_GSave (page);
    HPDF_Page_SetTextRenderingMode (page, HPDF_FILL_STROKE_CLIPPING);
    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, 60, ypos - 250, "ABCabc123");
    HPDF_Page_EndText (page);
    show_stripe_pattern (page, 60, ypos - 250);
    HPDF_Page_GRestore (page);

    /* Reset text attributes */
    HPDF_Page_SetTextRenderingMode (page, HPDF_FILL);
    HPDF_Page_SetRGBFill (page, 0, 0, 0);
    HPDF_Page_SetFontAndSize(page, font, 30);


    /*
     * Rotating text
     */
    angle1 = 30;                   /* A rotation of 30 degrees. */
    rad1 = angle1 / 180 * 3.141592; /* Calculate the radian value. */

    show_description (page, 320, ypos - 60, "Rotating text");
    HPDF_Page_BeginText (page);
    HPDF_Page_SetTextMatrix (page, cos(rad1), sin(rad1), -sin(rad1), cos(rad1),
                330, ypos - 60);
    HPDF_Page_ShowText (page, "ABCabc123");
    HPDF_Page_EndText (page);


    /*
     * Skewing text.
     */
    show_description (page, 320, ypos - 120, "Skewing text");
    HPDF_Page_BeginText (page);

    angle1 = 10;
    angle2 = 20;
    rad1 = angle1 / 180 * 3.141592;
    rad2 = angle2 / 180 * 3.141592;

    HPDF_Page_SetTextMatrix (page, 1, tan(rad1), tan(rad2), 1, 320, ypos - 120);
    HPDF_Page_ShowText (page, "ABCabc123");
    HPDF_Page_EndText (page);


    /*
     * scaling text (X direction)
     */
    show_description (page, 320, ypos - 175, "Scaling text (X direction)");
    HPDF_Page_BeginText (page);
    HPDF_Page_SetTextMatrix (page, 1.5, 0, 0, 1, 320, ypos - 175);
    HPDF_Page_ShowText (page, "ABCabc12");
    HPDF_Page_EndText (page);


    /*
     * scaling text (Y direction)
     */
    show_description (page, 320, ypos - 250, "Scaling text (Y direction)");
    HPDF_Page_BeginText (page);
    HPDF_Page_SetTextMatrix (page, 1, 0, 0, 2, 320, ypos - 250);
    HPDF_Page_ShowText (page, "ABCabc123");
    HPDF_Page_EndText (page);


    /*
     * char spacing, word spacing
     */

    show_description (page, 60, 140, "char-spacing 0");
    show_description (page, 60, 100, "char-spacing 1.5");
    show_description (page, 60, 60, "char-spacing 1.5, word-spacing 2.5");

    HPDF_Page_SetFontAndSize (page, font, 20);
    HPDF_Page_SetRGBFill (page, 0.1, 0.3, 0.1);

    /* char-spacing 0 */
    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, 60, 140, samp_text2);
    HPDF_Page_EndText (page);

    /* char-spacing 1.5 */
    HPDF_Page_SetCharSpace (page, 1.5);

    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, 60, 100, samp_text2);
    HPDF_Page_EndText (page);

    /* char-spacing 1.5, word-spacing 3.5 */
    HPDF_Page_SetWordSpace (page, 2.5);

    HPDF_Page_BeginText (page);
    HPDF_Page_TextOut (page, 60, 60, samp_text2);
    HPDF_Page_EndText (page);

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}

