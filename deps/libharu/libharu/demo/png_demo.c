/*
 * << Haru Free PDF Library 2.0.0 >> -- png_demo.c
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

#ifdef LIBHPDF_HAVE_LIBPNG

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
draw_image (HPDF_Doc     pdf,
            const char  *filename,
            float        x,
            float        y,
            const char  *text)
{
#ifdef __WIN32__
    const char* FILE_SEPARATOR = "\\";
#else
    const char* FILE_SEPARATOR = "/";
#endif
    char filename1[255];

    HPDF_Page page = HPDF_GetCurrentPage (pdf);
    HPDF_Image image;

    strcpy(filename1, "pngsuite");
    strcat(filename1, FILE_SEPARATOR);
    strcat(filename1, filename);

    image = HPDF_LoadPngImageFromFile (pdf, filename1);

    /* Draw image to the canvas. */
    HPDF_Page_DrawImage (page, image, x, y, HPDF_Image_GetWidth (image),
                    HPDF_Image_GetHeight (image));

    /* Print the text. */
    HPDF_Page_BeginText (page);
    HPDF_Page_SetTextLeading (page, 16);
    HPDF_Page_MoveTextPos (page, x, y);
    HPDF_Page_ShowTextNextLine (page, filename);
    HPDF_Page_ShowTextNextLine (page, text);
    HPDF_Page_EndText (page);
}


int main (int argc, char **argv)
{
    (void) argc; /* Not used */
    HPDF_Doc  pdf;
    HPDF_Font font;
    HPDF_Page page;
    char fname[256];
    HPDF_Destination dst;

    strcpy (fname, argv[0]);
    strcat (fname, ".pdf");

    pdf = HPDF_New (error_handler, NULL);
    if (!pdf) {
        printf ("error: cannot create PdfDoc object\n");
        return 1;
    }

    /* error-handler */
    if (setjmp(env)) {
        HPDF_Free (pdf);
        return 1;
    }

    HPDF_SetCompressionMode (pdf, HPDF_COMP_ALL);

    /* create default-font */
    font = HPDF_GetFont (pdf, "Helvetica", NULL);

    /* add a new page object. */
    page = HPDF_AddPage (pdf);

    HPDF_Page_SetWidth (page, 550);
    HPDF_Page_SetHeight (page, 650);

    dst = HPDF_Page_CreateDestination (page);
    HPDF_Destination_SetXYZ (dst, 0, HPDF_Page_GetHeight (page), 1);
    HPDF_SetOpenAction(pdf, dst);

    HPDF_Page_BeginText (page);
    HPDF_Page_SetFontAndSize (page, font, 20);
    HPDF_Page_MoveTextPos (page, 220, HPDF_Page_GetHeight (page) - 70);
    HPDF_Page_ShowText (page, "PngDemo");
    HPDF_Page_EndText (page);

    HPDF_Page_SetFontAndSize (page, font, 12);

    draw_image (pdf, "basn0g01.png", 100, HPDF_Page_GetHeight (page) - 150,
                "1bit grayscale.");
    draw_image (pdf, "basn0g02.png", 200, HPDF_Page_GetHeight (page) - 150,
                "2bit grayscale.");
    draw_image (pdf, "basn0g04.png", 300, HPDF_Page_GetHeight (page) - 150,
                "4bit grayscale.");
    draw_image (pdf, "basn0g08.png", 400, HPDF_Page_GetHeight (page) - 150,
                "8bit grayscale.");

    draw_image (pdf, "basn2c08.png", 100, HPDF_Page_GetHeight (page) - 250,
                "8bit color.");
    draw_image (pdf, "basn2c16.png", 200, HPDF_Page_GetHeight (page) - 250,
                "16bit color.");

    draw_image (pdf, "basn3p01.png", 100, HPDF_Page_GetHeight (page) - 350,
                "1bit pallet.");
    draw_image (pdf, "basn3p02.png", 200, HPDF_Page_GetHeight (page) - 350,
                "2bit pallet.");
    draw_image (pdf, "basn3p04.png", 300, HPDF_Page_GetHeight (page) - 350,
                "4bit pallet.");
    draw_image (pdf, "basn3p08.png", 400, HPDF_Page_GetHeight (page) - 350,
                "8bit pallet.");

    draw_image (pdf, "basn4a08.png", 100, HPDF_Page_GetHeight (page) - 450,
                "8bit alpha.");
    draw_image (pdf, "basn4a16.png", 200, HPDF_Page_GetHeight (page) - 450,
                "16bit alpha.");

    draw_image (pdf, "basn6a08.png", 100, HPDF_Page_GetHeight (page) - 550,
                "8bit alpha.");
    draw_image (pdf, "basn6a16.png", 200, HPDF_Page_GetHeight (page) - 550,
                "16bit alpha.");

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}

#else /* LIBHPDF_HAVE_LIBPNG */

int main()
{
    printf("WARNING: png_demo was not built correctly. \n"
           "Make sure libpng is installed and CMake is able to find it.\n");
    return 0;
}

#endif /* LIBHPDF_HAVE_LIBPNG */

