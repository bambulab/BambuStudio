/*
 * << Haru Free PDF Library 2.0.0 >> -- character_map.c
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
 * usage character_map <encoding-name> <low-range-from> <low-range-to>
 *              <high-range-from> <high-range-to>
 * ex. character_map 90ms-RKSJ-V 0x80 0x
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


void
draw_page  (HPDF_Page      page,
            HPDF_Font      title_font,
            HPDF_Font      font,
            HPDF_BYTE      h_byte,
            HPDF_BYTE      l_byte)
{
    const int PAGE_WIDTH = 420;
    const int CELL_HEIGHT = 20;
    const int CELL_WIDTH = 20;

    unsigned int h_count;
    int xpos, ypos;
    unsigned int page_height;

    l_byte = (int)(l_byte / 16) * 16;
    h_count = 16 - (l_byte / 16);
    page_height = 40 + 40 + (h_count + 1) * CELL_HEIGHT;

    HPDF_Page_SetHeight (page, page_height);
    HPDF_Page_SetWidth (page, PAGE_WIDTH);

    HPDF_Page_SetFontAndSize (page, title_font, 10);

    ypos = h_count + 1;
    for (;;) {
        int y = (ypos) * CELL_HEIGHT + 40;

        HPDF_Page_MoveTo (page, 40, y);
        HPDF_Page_LineTo (page, 380, y);
        HPDF_Page_Stroke (page);
        if (ypos < h_count) {
            unsigned char buf[2];
            double w;

            buf[0] = 16 - ypos - 1;
            if (buf[0] < 10)
                buf[0] += '0';
            else
                buf[0] += ('A' - 10);
            buf[1] = 0;

            w = HPDF_Page_TextWidth (page, (char*)buf);
            HPDF_Page_BeginText (page);
            HPDF_Page_MoveTextPos (page, 40 + ((double)20 - w) / 2, y + 5);
            HPDF_Page_ShowText (page, (char*)buf);
            HPDF_Page_EndText (page);
        }

        if (ypos == 0)
            break;

        ypos--;
    }

    for (xpos = 0; xpos <= 17; xpos++) {
        int y = (h_count + 1) * CELL_HEIGHT + 40;
        int x = xpos * CELL_WIDTH + 40;

        HPDF_Page_MoveTo (page, x, 40);
        HPDF_Page_LineTo (page, x, y);
        HPDF_Page_Stroke (page);

        if (xpos > 0 && xpos <= 16) {
            unsigned char buf[2];
            double w;

            buf[0] = xpos - 1;
            if (buf[0] < 10)
                buf[0] += '0';
            else
                buf[0] += ('A' - 10);
            buf[1] = 0;

            w = HPDF_Page_TextWidth(page, (char*)buf);
            HPDF_Page_BeginText(page);
            HPDF_Page_MoveTextPos(page, x + ((double)20 - w) / 2,
                        h_count * CELL_HEIGHT + 45);
            HPDF_Page_ShowText(page, (char*)buf);
            HPDF_Page_EndText(page);
        }
    }

    HPDF_Page_SetFontAndSize (page, font, 15);

    ypos = h_count;
    for (;;) {
        int y = (ypos - 1) * CELL_HEIGHT + 45;

        for (xpos = 0; xpos < 16; xpos++) {
            unsigned char buf[3];
            double w;

            int x = xpos * CELL_WIDTH + 40 + CELL_WIDTH;

            buf[0] = h_byte;
            buf[1] = (16 - ypos) * 16 + xpos;
            buf[2] = 0x00;

            w = HPDF_Page_TextWidth(page, (char*)buf);
            if (w > 0) {
                HPDF_Page_BeginText(page);
                HPDF_Page_MoveTextPos(page, x + ((double)20 - w) / 2, y);
                HPDF_Page_ShowText(page, (char*)buf);
                HPDF_Page_EndText(page);
            }
        }

        if (ypos == 0)
            break;

        ypos--;
    }

}

int
main  (int      argc,
       char   **argv)
{
    unsigned int i, j;
    unsigned int min_l, max_l, min_h, max_h;
    char fname[256];
    HPDF_UINT16 flg[256];

    HPDF_Doc pdf;
    HPDF_Encoder encoder;
    HPDF_Font font;
    HPDF_Outline root;

    strcpy (fname, argv[0]);
    strcat (fname, ".pdf");

    if (argc < 3) {
        printf ("usage: character_map <encoding-name> <font-name>\n");
        return 1;
    }

    pdf = HPDF_New (error_handler, NULL);
    if (!pdf) {
        printf ("error: cannot create PdfDoc object\n");
        return 1;
    }

    if (setjmp(env)) {
        HPDF_Free (pdf);
        return 1;
    }

    /* configure pdf-document (showing outline, compression enabled) */
    HPDF_SetPageMode(pdf, HPDF_PAGE_MODE_USE_OUTLINE);
    HPDF_SetCompressionMode (pdf, HPDF_COMP_ALL);
    HPDF_SetPagesConfiguration (pdf, 10);

    HPDF_UseJPEncodings (pdf);
    HPDF_UseJPFonts (pdf);
    HPDF_UseKREncodings (pdf);
    HPDF_UseKRFonts (pdf);
    HPDF_UseCNSEncodings (pdf);
    HPDF_UseCNSFonts (pdf);
    HPDF_UseCNTEncodings (pdf);
    HPDF_UseCNTFonts (pdf);

    encoder = HPDF_GetEncoder (pdf, argv[1]);
    if (HPDF_Encoder_GetType (encoder) != HPDF_ENCODER_TYPE_DOUBLE_BYTE) {
        printf ("error: %s is not cmap-encoder\n", argv[1]);
        HPDF_Free (pdf);
        return 1;
    }

    font = HPDF_GetFont (pdf, argv[2], argv[1]);

    min_l = 255;
    min_h = 256;
    max_l = 0;
    max_h = 0;

    for (i = 0; i <= 255; i++) {
        flg[i] = 0;
    }

    for (i = 0; i <= 255; i++) {
        for (j = 20; j <= 255; j++) {
            unsigned char buf[3];
            HPDF_ByteType btype;
            HPDF_UINT16 code = i * 256 + j;
            HPDF_UNICODE unicode;

            buf[0] = i;
            buf[1] = j;
            buf[2] = 0;

            btype = HPDF_Encoder_GetByteType (encoder, buf, 0);
            unicode = HPDF_Encoder_GetUnicode (encoder, code);

            if (btype == HPDF_BYTE_TYPE_LEAD &&
                    unicode != 0x25A1) {
                if (min_l > j)
                    min_l = j;

                if (max_l < j)
                    max_l = j;

                if (min_h > i)
                    min_h = i;

                if (max_h < i)
                    max_h = i;

                flg[i] = 1;
            }
        }
    }

    printf ("min_h=%04X max_h=%04X min_l=%04X max_l=%04X\n",
            min_h, max_h, min_l, max_l);

    /* create outline root. */
    root = HPDF_CreateOutline (pdf, NULL, argv[1], NULL);
    HPDF_Outline_SetOpened (root, HPDF_TRUE);

    for (i = 0; i <= 255; i++) {
        if (flg[i]) {
            char buf[256];
            HPDF_Page page = HPDF_AddPage (pdf);
            HPDF_Font title_font = HPDF_GetFont (pdf, "Helvetica", NULL);
            HPDF_Outline outline;
            HPDF_Destination dst;
#ifdef __WIN32__
            _snprintf (buf, 256, "0x%04X-0x%04X",
                    (unsigned int)(i * 256 + min_l),
                    (unsigned int)(i * 256 + max_l));
#else
            snprintf (buf, 256, "0x%04X-0x%04X",
                    (unsigned int)(i * 256 + min_l),
                    (unsigned int)(i * 256 + max_l));
#endif
    outline = HPDF_CreateOutline (pdf, root, buf, NULL);
            dst = HPDF_Page_CreateDestination (page);
            HPDF_Outline_SetDestination(outline, dst);

            draw_page (page, title_font, font, (HPDF_BYTE)i, (HPDF_BYTE)min_l);

#ifdef __WIN32__
            _snprintf (buf, 256, "%s (%s) 0x%04X-0x%04X", argv[1], argv[2],
                        (unsigned int)(i * 256 + min_l),
                        (unsigned int)(i * 256 + max_l));
#else
            snprintf (buf, 256, "%s (%s) 0x%04X-0x%04X", argv[1], argv[2],
                        (unsigned int)(i * 256 + min_l),
                        (unsigned int)(i * 256 + max_l));
#endif
            HPDF_Page_SetFontAndSize (page, title_font, 10);
            HPDF_Page_BeginText (page);
            HPDF_Page_MoveTextPos (page, 40, HPDF_Page_GetHeight (page) - 35);
            HPDF_Page_ShowText (page, buf);
            HPDF_Page_EndText (page);
        }
    }

    HPDF_SaveToFile (pdf, fname);
    HPDF_Free (pdf);

    return 0;
}
