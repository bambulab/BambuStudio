/*
 * << Haru Free PDF Library 2.0.0 >> -- image_demo.c
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
show_description (HPDF_Page    page,
                  float        x,
                  float        y,
                  const char  *text)
{
    char buf[255];

    HPDF_Page_MoveTo (page, x, y - 10);
    HPDF_Page_LineTo (page, x, y + 10);
    HPDF_Page_MoveTo (page, x - 10, y);
    HPDF_Page_LineTo (page, x + 10, y);
    HPDF_Page_Stroke (page);

    HPDF_Page_SetFontAndSize (page, HPDF_Page_GetCurrentFont (page), 8);
    HPDF_Page_SetRGBFill (page, 0, 0, 0);

    HPDF_Page_BeginText (page);

#ifdef __WIN32__
    _snprintf(buf, 255, "(x=%d,y=%d)", (int)x, (int)y);
#else
    snprintf(buf, 255, "(x=%d,y=%d)", (int)x, (int)y);
#endif /* __WIN32__ */
    HPDF_Page_MoveTextPos (page, x - HPDF_Page_TextWidth (page, buf) - 5,
            y - 10);
    HPDF_Page_ShowText (page, buf);
    HPDF_Page_EndText (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, x - 20, y - 25);
    HPDF_Page_ShowText (page, text);
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
    HPDF_Image image;
    HPDF_Image image1;
    HPDF_Image image2;
    HPDF_Image image3;

    double x;
    double y;
    double angle;
    double angle1;
    double angle2;
    double rad;
    double rad1;
    double rad2;

    double iw;
    double ih;

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
    HPDF_Page_SetHeight (page, 500);

    dst = HPDF_Page_CreateDestination (page);
    HPDF_Destination_SetXYZ (dst, 0, HPDF_Page_GetHeight (page), 1);
    HPDF_SetOpenAction(pdf, dst);

    HPDF_Page_BeginText (page);
    HPDF_Page_SetFontAndSize (page, font, 20);
    HPDF_Page_MoveTextPos (page, 220, HPDF_Page_GetHeight (page) - 70);
    HPDF_Page_ShowText (page, "ImageDemo");
    HPDF_Page_EndText (page);

    /* load image file. */
    #ifndef __WIN32__
    image = HPDF_LoadPngImageFromFile (pdf, "pngsuite/basn3p02.png");
    #else
    image = HPDF_LoadPngImageFromFile (pdf, "pngsuite\\basn3p02.png");
    #endif

    /* image1 is masked by image2. */
    #ifndef __WIN32__
    image1 = HPDF_LoadPngImageFromFile (pdf, "pngsuite/basn3p02.png");
    #else
    image1 = HPDF_LoadPngImageFromFile (pdf, "pngsuite\\basn3p02.png");
    #endif

    /* image2 is a mask image. */
    #ifndef __WIN32__
    image2 = HPDF_LoadPngImageFromFile (pdf, "pngsuite/basn0g01.png");
    #else
    image2 = HPDF_LoadPngImageFromFile (pdf, "pngsuite\\basn0g01.png");
    #endif

    /* image3 is a RGB-color image. we use this image for color-mask
     * demo.
     */
    #ifndef __WIN32__
    image3 = HPDF_LoadPngImageFromFile (pdf, "pngsuite/maskimage.png");
    #else
    image3 = HPDF_LoadPngImageFromFile (pdf, "pngsuite\\maskimage.png");
    #endif

    iw = HPDF_Image_GetWidth (image);
    ih = HPDF_Image_GetHeight (image);

    HPDF_Page_SetLineWidth (page, 0.5);

    x = 100;
    y = HPDF_Page_GetHeight (page) - 150;

    /* Draw image to the canvas. (normal-mode with actual size.)*/
    HPDF_Page_DrawImage (page, image, x, y, iw, ih);

    show_description (page, x, y, "Actual Size");

    x += 150;

    /* Scalling image (X direction) */
    HPDF_Page_DrawImage (page, image, x, y, iw * 1.5, ih);

    show_description (page, x, y, "Scalling image (X direction)");

    x += 150;

    /* Scalling image (Y direction). */
    HPDF_Page_DrawImage (page, image, x, y, iw, ih * 1.5);
    show_description (page, x, y, "Scalling image (Y direction)");

    x = 100;
    y -= 120;

    /* Skewing image. */
    angle1 = 10;
    angle2 = 20;
    rad1 = angle1 / 180 * 3.141592;
    rad2 = angle2 / 180 * 3.141592;

    HPDF_Page_GSave (page);

    HPDF_Page_Concat (page, iw, tan(rad1) * iw, tan(rad2) * ih, ih, x, y);

    HPDF_Page_ExecuteXObject (page, image);
    HPDF_Page_GRestore (page);

    show_description (page, x, y, "Skewing image");

    x += 150;

    /* Rotating image */
    angle = 30;     /* rotation of 30 degrees. */
    rad = angle / 180 * 3.141592; /* Calculate the radian value. */

    HPDF_Page_GSave (page);

    HPDF_Page_Concat (page, iw * cos(rad),
                iw * sin(rad),
                ih * -sin(rad),
                ih * cos(rad),
                x, y);

    HPDF_Page_ExecuteXObject (page, image);
    HPDF_Page_GRestore (page);

    show_description (page, x, y, "Rotating image");

        x += 150;

    /* draw masked image. */

    /* Set image2 to the mask image of image1 */
    HPDF_Image_SetMaskImage (image1, image2);

    HPDF_Page_SetRGBFill (page, 0, 0, 0);
    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, x - 6, y + 14);
    HPDF_Page_ShowText (page, "MASKMASK");
    HPDF_Page_EndText (page);

    HPDF_Page_DrawImage (page, image1, x - 3, y - 3, iw + 6, ih + 6);

    show_description (page, x, y, "masked image");

        x = 100;
        y -= 120;

    /* color mask. */
    HPDF_Page_SetRGBFill (page, 0, 0, 0);
    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, x - 6, y + 14);
    HPDF_Page_ShowText (page, "MASKMASK");
    HPDF_Page_EndText (page);

    HPDF_Image_SetColorMask (image3, 0, 255, 0, 0, 0, 255);
    HPDF_Page_DrawImage (page, image3, x, y, iw, ih);

    show_description (page, x, y, "Color Mask");

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}

#else /* LIBHPDF_HAVE_LIBPNG */

int main()
{
    printf("WARNING: image_demo was not built correctly. \n"
           "Make sure libpng is installed and CMake is able to find it.\n");
    return 0;
}

#endif /* LIBHPDF_HAVE_LIBPNG */

