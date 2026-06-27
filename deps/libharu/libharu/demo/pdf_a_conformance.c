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

/* Text */
static char text1[] = "This PDF should have an attachment named factur-x.xml";
static char text2[] = "and should be PDF-A/3 compliant.";

/* Some hard coded PDF dates */
static HPDF_Date creation_date()
{
    HPDF_Date date;
    date.year = 2024;
    date.month = 1;
    date.day = 20;
    date.hour = 17;
    date.minutes = 10;
    date.seconds = 30;
    date.ind = '+';
    date.off_hour = 1;
    date.off_minutes = 0;
    return date;
}

static HPDF_Date modification_date()
{
    HPDF_Date date;
    date.year = 2024;
    date.month = 2;
    date.day = 5;
    date.hour = 11;
    date.minutes = 14;
    date.seconds = 45;
    date.ind = '+';
    date.off_hour = 1;
    date.off_minutes = 0;
    return date;
}

/* XMP extensions */
static char FactureX_1[] =
    "<rdf:Description xmlns:pdfaExtension=\"http://www.aiim.org/pdfa/ns/extension/\" xmlns:pdfaSchema=\"http://www.aiim.org/pdfa/ns/schema#\" xmlns:pdfaProperty=\"http://www.aiim.org/pdfa/ns/property#\" rdf:about=\"\">"
    "  <pdfaExtension:schemas>"
    "    <rdf:Bag>"
    "      <rdf:li rdf:parseType=\"Resource\">"
    "        <pdfaSchema:schema>Factur-X PDFA Extension Schema</pdfaSchema:schema>"
    "        <pdfaSchema:namespaceURI>urn:factur-x:pdfa:CrossIndustryDocument:invoice:1p0#</pdfaSchema:namespaceURI>"
    "        <pdfaSchema:prefix>fx</pdfaSchema:prefix>"
    "        <pdfaSchema:property>"
    "          <rdf:Seq>"
    "            <rdf:li rdf:parseType=\"Resource\">"
    "              <pdfaProperty:name>DocumentFileName</pdfaProperty:name>"
    "              <pdfaProperty:valueType>Text</pdfaProperty:valueType>"
    "              <pdfaProperty:category>external</pdfaProperty:category>"
    "              <pdfaProperty:description>The name of the embedded XML document</pdfaProperty:description>"
    "            </rdf:li>"
    "            <rdf:li rdf:parseType=\"Resource\">"
    "              <pdfaProperty:name>DocumentType</pdfaProperty:name>"
    "              <pdfaProperty:valueType>Text</pdfaProperty:valueType>"
    "              <pdfaProperty:category>external</pdfaProperty:category>"
    "              <pdfaProperty:description>The type of the hybrid document in capital letters, e.g. INVOICE or ORDER</pdfaProperty:description>"
    "            </rdf:li>"
    "            <rdf:li rdf:parseType=\"Resource\">"
    "              <pdfaProperty:name>Version</pdfaProperty:name>"
    "              <pdfaProperty:valueType>Text</pdfaProperty:valueType>"
    "              <pdfaProperty:category>external</pdfaProperty:category>"
    "              <pdfaProperty:description>The actual version of the standard applying to the embedded XML document</pdfaProperty:description>"
    "            </rdf:li>"
    "            <rdf:li rdf:parseType=\"Resource\">"
    "              <pdfaProperty:name>ConformanceLevel</pdfaProperty:name>"
    "              <pdfaProperty:valueType>Text</pdfaProperty:valueType>"
    "              <pdfaProperty:category>external</pdfaProperty:category>"
    "              <pdfaProperty:description>The conformance level of the embedded XML document</pdfaProperty:description>"
    "            </rdf:li>"
    "          </rdf:Seq>"
    "        </pdfaSchema:property>"
    "      </rdf:li>"
    "    </rdf:Bag>"
    "  </pdfaExtension:schemas>"
    "</rdf:Description>"
    ;

static char FactureX_2[] =
    "<rdf:Description xmlns:fx=\"urn:factur-x:pdfa:CrossIndustryDocument:invoice:1p0#\" rdf:about=\"\">"
    "  <fx:DocumentType>INVOICE</fx:DocumentType>"
    "  <fx:DocumentFileName>factur-x.xml</fx:DocumentFileName>"
    "  <fx:Version>1.0</fx:Version>"
    "  <fx:ConformanceLevel>EN 16931</fx:ConformanceLevel>"
    "</rdf:Description>"
    ;

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
    const char *font_name;
    HPDF_Font font;
    HPDF_Page page;
    HPDF_EmbeddedFile embedded_file;
    HPDF_OutputIntent outputIntents;
    char fname[256];
    HPDF_REAL tw;

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

    /* create default-font */
    font_name = HPDF_LoadTTFontFromFile (pdf, "ttfont/PenguinAttack.ttf", 1);
    font = HPDF_GetFont (pdf, font_name, "WinAnsiEncoding");

    /* add a new page object. */
    page = HPDF_AddPage (pdf);

    HPDF_Page_SetSize (page, HPDF_PAGE_SIZE_LETTER, HPDF_PAGE_PORTRAIT);

    HPDF_Page_BeginText (page);
    HPDF_Page_SetFontAndSize (page, font, 20);
    tw = HPDF_Page_TextWidth (page, text1);
    HPDF_Page_MoveTextPos (page, (HPDF_Page_GetWidth (page) - tw) / 2, (HPDF_Page_GetHeight (page)) / 2);
    HPDF_Page_ShowText (page, text1);
    tw -= HPDF_Page_TextWidth (page, text2);
    HPDF_Page_MoveTextPos (page, tw / 2, -20);
    HPDF_Page_ShowText (page, text2);
    HPDF_Page_EndText (page);

    /* attach a file to the document, with some info */
    embedded_file = HPDF_AttachFile (pdf, "pdf_a/factur-x.xml");
    HPDF_EmbeddedFile_SetAFRelationship(embedded_file, HPDF_AFRELATIONSHIP_DATA);
    HPDF_EmbeddedFile_SetSize(embedded_file, 17167);
    HPDF_EmbeddedFile_SetName(embedded_file, "factur-x.xml");
    HPDF_EmbeddedFile_SetDescription(embedded_file, "Factur-X invoice");
    HPDF_EmbeddedFile_SetSubtype(embedded_file, "text/xml");
    HPDF_EmbeddedFile_SetCreationDate(embedded_file, creation_date());
    HPDF_EmbeddedFile_SetCreationDate(embedded_file, modification_date());

    /* some PDF info */
    HPDF_SetInfoAttr (pdf, HPDF_INFO_TITLE, "PDF-A Title");
    HPDF_SetInfoAttr (pdf, HPDF_INFO_SUBJECT, "PDF-A Subject");
    HPDF_SetInfoAttr (pdf, HPDF_INFO_AUTHOR, "PDF-A Author");
    HPDF_SetInfoAttr (pdf, HPDF_INFO_CREATOR, "libharu");
    HPDF_SetInfoDateAttr (pdf, HPDF_INFO_CREATION_DATE, creation_date());
    HPDF_SetInfoDateAttr (pdf, HPDF_INFO_MOD_DATE, modification_date());

    /* PDF-A/3 conformance */
    HPDF_SetPDFAConformance (pdf, HPDF_PDFA_3B);

    /* append outputintents */
    outputIntents = HPDF_LoadIccProfileFromFile (pdf, "pdf_a/device_rgb.icc", 3);
    HPDF_AppendOutputIntents (pdf, "sRGB", outputIntents);

    /* XMP extensions */
    HPDF_AddPDFAXmpExtension (pdf, FactureX_1);
    HPDF_AddPDFAXmpExtension (pdf, FactureX_2);

    /* save the document to a file */
    HPDF_SaveToFile (pdf, fname);

    /* clean up */
    HPDF_Free (pdf);

    return 0;
}

