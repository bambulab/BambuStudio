/*
 * << Haru Free PDF Library 2.0.0 >> -- raw_image_demo.c
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

const HPDF_BYTE RAW_IMAGE_DATA[128] = {
    0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xfc,
    0xff, 0xff, 0xff, 0xf8, 0xff, 0xff, 0xff, 0xf0,
    0xf3, 0xf3, 0xff, 0xe0, 0xf3, 0xf3, 0xff, 0xc0,
    0xf3, 0xf3, 0xff, 0x80, 0xf3, 0x33, 0xff, 0x00,
    0xf3, 0x33, 0xfe, 0x00, 0xf3, 0x33, 0xfc, 0x00,
    0xf8, 0x07, 0xf8, 0x00, 0xf8, 0x07, 0xf0, 0x00,
    0xfc, 0xcf, 0xe0, 0x00, 0xfc, 0xcf, 0xc0, 0x00,
    0xff, 0xff, 0x80, 0x00, 0xff, 0xff, 0x00, 0x00,
    0xff, 0xfe, 0x00, 0x00, 0xff, 0xfc, 0x00, 0x00,
    0xff, 0xf8, 0x0f, 0xe0, 0xff, 0xf0, 0x0f, 0xe0,
    0xff, 0xe0, 0x0c, 0x30, 0xff, 0xc0, 0x0c, 0x30,
    0xff, 0x80, 0x0f, 0xe0, 0xff, 0x00, 0x0f, 0xe0,
    0xfe, 0x00, 0x0c, 0x30, 0xfc, 0x00, 0x0c, 0x30,
    0xf8, 0x00, 0x0f, 0xe0, 0xf0, 0x00, 0x0f, 0xe0,
    0xe0, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

int main (int argc, char **argv)
{
    (void) argc; /* Not used */
    HPDF_Doc  pdf;
    HPDF_Font font;
    HPDF_Page page;
    char fname[256];
    HPDF_Image image;

    HPDF_REAL x, y;

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

    HPDF_Page_SetWidth (page, 172);
    HPDF_Page_SetHeight (page, 80);

    HPDF_Page_BeginText (page);
    HPDF_Page_SetFontAndSize (page, font, 20);
    HPDF_Page_MoveTextPos (page, 220, HPDF_Page_GetHeight (page) - 70);
    HPDF_Page_ShowText (page, "RawImageDemo");
    HPDF_Page_EndText (page);

    /* load RGB raw-image file. */
    #ifndef __WIN32__
    image = HPDF_LoadRawImageFromFile (pdf, "rawimage/32_32_rgb.dat",
            32, 32, HPDF_CS_DEVICE_RGB);
    #else
    image = HPDF_LoadRawImageFromFile (pdf, "rawimage\\32_32_rgb.dat",
            32, 32, HPDF_CS_DEVICE_RGB);
    #endif

    x = 20;
    y = 20;

    /* Draw image to the canvas. (normal-mode with actual size.)*/
    HPDF_Page_DrawImage (page, image, x, y, 32, 32);

    /* load GrayScale raw-image file. */
    #ifndef __WIN32__
    image = HPDF_LoadRawImageFromFile (pdf, "rawimage/32_32_gray.dat",
            32, 32, HPDF_CS_DEVICE_GRAY);
    #else
    image = HPDF_LoadRawImageFromFile (pdf, "rawimage\\32_32_gray.dat",
            32, 32, HPDF_CS_DEVICE_GRAY);
    #endif

    x = 70;
    y = 20;

    /* Draw image to the canvas. (normal-mode with actual size.)*/
    HPDF_Page_DrawImage (page, image, x, y, 32, 32);

    /* load GrayScale raw-image (1bit) file from memory. */
    image = HPDF_LoadRawImageFromMem (pdf, RAW_IMAGE_DATA, 32, 32,
                HPDF_CS_DEVICE_GRAY, 1);

    x = 120;
    y = 20;

    /* Draw image to the canvas. (normal-mode with actual size.)*/
    HPDF_Page_DrawImage (page, image, x, y, 32, 32);

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}


