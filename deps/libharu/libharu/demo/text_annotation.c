/*
 * << Haru Free PDF Library 2.0.0 >> -- text_annotation.c
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


int main(int argc, char **argv)
{
    (void) argc; /* Not used */
    HPDF_Rect rect1 = {50, 350, 150, 400};
    HPDF_Rect rect2 = {210, 350, 350, 400};
    HPDF_Rect rect3 = {50, 250, 150, 300};
    HPDF_Rect rect4 = {210, 250, 350, 300};
    HPDF_Rect rect5 = {50, 150, 150, 200};
    HPDF_Rect rect6 = {210, 150, 350, 200};
    HPDF_Rect rect7 = {50, 50, 150, 100};
    HPDF_Rect rect8 = {210, 50, 350, 100};

    HPDF_Doc  pdf;
    char fname[256];
    HPDF_Page page;
    HPDF_Font font;
    HPDF_Encoder encoding;
    HPDF_Annotation annot;

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

    /* use Times-Roman font. */
    font = HPDF_GetFont (pdf, "Times-Roman", "WinAnsiEncoding");

    page = HPDF_AddPage (pdf);

    HPDF_Page_SetWidth (page, 400);
    HPDF_Page_SetHeight (page, 500);

    HPDF_Page_BeginText (page);
    HPDF_Page_SetFontAndSize (page, font, 16);
    HPDF_Page_MoveTextPos (page, 130, 450);
    HPDF_Page_ShowText (page, "Annotation Demo");
    HPDF_Page_EndText (page);


    annot = HPDF_Page_CreateTextAnnot (page, rect1, "Annotation with Comment "
                "Icon. \n This annotation set to be opened initially.",
                NULL);

    HPDF_TextAnnot_SetIcon (annot, HPDF_ANNOT_ICON_COMMENT);
    HPDF_TextAnnot_SetOpened (annot, HPDF_TRUE);

    annot = HPDF_Page_CreateTextAnnot (page, rect2,
                "Annotation with Key Icon", NULL);
    HPDF_TextAnnot_SetIcon (annot, HPDF_ANNOT_ICON_PARAGRAPH);

    annot = HPDF_Page_CreateTextAnnot (page, rect3,
                "Annotation with Note Icon", NULL);
    HPDF_TextAnnot_SetIcon (annot, HPDF_ANNOT_ICON_NOTE);

    annot = HPDF_Page_CreateTextAnnot (page, rect4,
                "Annotation with Help Icon", NULL);
    HPDF_TextAnnot_SetIcon (annot, HPDF_ANNOT_ICON_HELP);

    annot = HPDF_Page_CreateTextAnnot (page, rect5,
                "Annotation with NewParagraph Icon", NULL);
    HPDF_TextAnnot_SetIcon (annot, HPDF_ANNOT_ICON_NEW_PARAGRAPH);

    annot = HPDF_Page_CreateTextAnnot (page, rect6,
                "Annotation with Paragraph Icon", NULL);
    HPDF_TextAnnot_SetIcon (annot, HPDF_ANNOT_ICON_PARAGRAPH);

    annot = HPDF_Page_CreateTextAnnot (page, rect7,
                "Annotation with Insert Icon", NULL);
    HPDF_TextAnnot_SetIcon (annot, HPDF_ANNOT_ICON_INSERT);

    encoding = HPDF_GetEncoder (pdf, "ISO8859-2");

    HPDF_Page_CreateTextAnnot (page, rect8,
                "Annotation with ISO8859 text гдежзий", encoding);

    HPDF_Page_SetFontAndSize (page, font, 11);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, rect1.left + 35, rect1.top - 20);
    HPDF_Page_ShowText (page, "Comment Icon.");
    HPDF_Page_EndText (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, rect2.left + 35, rect2.top - 20);
    HPDF_Page_ShowText (page, "Key Icon");
    HPDF_Page_EndText (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, rect3.left + 35, rect3.top - 20);
    HPDF_Page_ShowText (page, "Note Icon.");
    HPDF_Page_EndText (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, rect4.left + 35, rect4.top - 20);
    HPDF_Page_ShowText (page, "Help Icon");
    HPDF_Page_EndText (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, rect5.left + 35, rect5.top - 20);
    HPDF_Page_ShowText (page, "NewParagraph Icon");
    HPDF_Page_EndText (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, rect6.left + 35, rect6.top - 20);
    HPDF_Page_ShowText (page, "Paragraph Icon");
    HPDF_Page_EndText (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, rect7.left + 35, rect7.top - 20);
    HPDF_Page_ShowText (page, "Insert Icon");
    HPDF_Page_EndText (page);

    HPDF_Page_BeginText (page);
    HPDF_Page_MoveTextPos (page, rect8.left + 35, rect8.top - 20);
    HPDF_Page_ShowText (page, "Text Icon(ISO8859-2 text)");
    HPDF_Page_EndText (page);


    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}
