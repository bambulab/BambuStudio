Attribute VB_Name = "Module1"
''/*
' * << Haru Free PDF Library 2.0.3 >> -- hpdf.h
' *
' * URL http://libharu.org/
' *
' * Copyright (c) 1999-2006 Takeshi Kanno
' *
' * Permission to use, copy, modify, distribute and sell this software
' * and its documentation for any purpose is hereby granted without fee,
' * provided that the above copyright notice appear in all copies and
' * that both that copyright notice and this permission notice appear
' * in supporting documentation.
' * It is provided "as is" without express or implied warranty.
' *
' */
Public sTemp1  As String
Public sTemp2  As String


'#include "hpdf_consts.bas"
'#include "hpdf_types.bas"

'typedef void         *HPDF_HANDLE
'typedef HPDF_HANDLE   long
'typedef HPDF_HANDLE   long
'typedef HPDF_HANDLE   longs
'typedef HPDF_HANDLE   HPDF_Stream
'typedef HPDF_HANDLE   HPDF_Image
'typedef HPDF_HANDLE   HPDF_Font
'typedef HPDF_HANDLE   HPDF_Outline
'typedef HPDF_HANDLE   HPDF_Encoder
'typedef HPDF_HANDLE   HPDF_Destination
'typedef HPDF_HANDLE   HPDF_XObject
'typedef HPDF_HANDLE   HPDF_Annotation


Public Declare Sub CopyMemory Lib "kernel32" Alias "RtlMoveMemory" (hpvDest As Any, hpvSource As Any, ByVal cbCopy As Long)

Public Declare Function HPDF_GetVersion Lib "libhpdf.dll" () As String
Public Declare Function HPDF_NewEx Lib "libhpdf.dll" (ByVal user_error_fn As Long, ByVal user_alloc_fn As Long, ByVal user_free_fn As Long, ByVal mem_pool_buf_size As Long, ByVal user_data As Any) As Long
Public Declare Function HPDF_New Lib "libhpdf.dll" (ByVal user_error_fn As Long, ByVal user_data As Any) As Long
Public Declare Function HPDF_SetErrorHandler Lib "libhpdf.dll" (ByVal pdf As Long, ByVal user_error_fn As Long) As Long
Public Declare Sub HPDF_Free Lib "libhpdf.dll" (ByVal pdf As Long)
Public Declare Function HPDF_NewDoc Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Sub HPDF_FreeDoc Lib "libhpdf.dll" (ByVal pdf As Long)
Public Declare Function HPDF_HasDoc Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_FreeDocAll Lib "libhpdf.dll" (ByVal pdf As Long)
Public Declare Function HPDF_SaveToStream Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_GetStreamSize Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_ReadFromStream Lib "libhpdf.dll" (ByVal pdf As Long, ByRef byt_buf As Long, ByRef lng_size As Long) As Long
Public Declare Function HPDF_ResetStream Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_SaveToFile Lib "libhpdf.dll" (ByVal pdf As Long, ByVal file_name As String) As Long
Public Declare Function HPDF_GetError Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_GetErrorDetail Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_ResetError Lib "libhpdf.dll" (ByVal pdf As Long)
Public Declare Function HPDF_SetPagesConfiguration Lib "libhpdf.dll" (ByVal pdf As Long, ByVal page_per_pages As Long) As Long
Public Declare Function HPDF_GetPageByIndex Lib "libhpdf.dll" (ByVal pdf As Long, ByVal index As Long) As Long
'/*---------------------------------------------------------------------------*/
'/*---------------------------------------------------------------------------*/
Public Declare Function HPDF_GetPageLayout Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_SetPageLayout Lib "libhpdf.dll" (ByVal pdf As Long, ByVal Layout As Long) As Long
Public Declare Function HPDF_GetPageMode Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_SetPageMode Lib "libhpdf.dll" (ByVal pdf As Long, ByVal page_mode As Long) As Long
Public Declare Function HPDF_GetViewerPreference Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_SetViewerPreference Lib "libhpdf.dll" (ByVal pdf As Long, ByVal value As Long) As Long
Public Declare Function HPDF_SetOpenAction Lib "libhpdf.dll" (ByVal pdf As Long, ByVal open_action As Long) As Long
'/*---------------------------------------------------------------------------*/
'/*----- page handling -------------------------------------------------------*/
Public Declare Function HPDF_GetCurrentPage Lib "libhpdf.dll" (pdf As Long) As Long
Public Declare Function HPDF_AddPage Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_InsertPage Lib "libhpdf.dll" (pdf As Long, page As Long) As Long
Public Declare Function HPDF_Page_SetWidth Lib "libhpdf.dll" (page As Long, value As Single) As Long
Public Declare Function HPDF_Page_SetHeight Lib "libhpdf.dll" (page As Long, value As Single) As Long
Public Declare Function HPDF_Page_SetSize Lib "libhpdf.dll" (page As Long, ByVal Size As Long, direction As Long) As Long
Public Declare Function HPDF_Page_SetRotate Lib "libhpdf.dll" (ByVal page As Long, ByVal angle As Integer) As Long
'/*---------------------------------------------------------------------------*/
'/*----- font handling -------------------------------------------------------*/
Public Declare Function HPDF_GetFont Lib "libhpdf.dll" (ByVal pdf As Long, ByVal font_name As String, ByVal encoding_name As String) As Long
Public Declare Function HPDF_LoadType1FontFromFile Lib "libhpdf.dll" (ByVal pdf As Long, ByVal afm_file_name As String, ByVal data_file_name As String) As String
Public Declare Function HPDF_LoadTTFontFromFile Lib "libhpdf.dll" (ByVal pdf As Long, ByVal file_name As String, ByVal embedding As Long) As String
Public Declare Function HPDF_LoadTTFontFromFile2 Lib "libhpdf.dll" (ByVal pdf As Long, ByVal file_name As String, ByVal index As Long, ByVal embedding As Long) As String
Public Declare Function HPDF_AddPageLabel Lib "libhpdf.dll" (ByVal pdf As Long, ByVal page_num As Long, ByVal style As Long, ByVal first_page As Long, ByVal prefix As String) As Long
Public Declare Function HPDF_UseJPFonts Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_UseKRFonts Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_UseCNSFonts Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_UseCNTFonts Lib "libhpdf.dll" (ByVal pdf As Long) As Long
'/*--------------------------------------------------------------------------*/
'/*----- outline ------------------------------------------------------------*/
Public Declare Function HPDF_CreateOutline Lib "libhpdf.dll" (ByVal pdf As Long, parent As Long, ByVal title As String, encoder As Long) As Long
Public Declare Function HPDF_Outline_SetOpened Lib "libhpdf.dll" (outline As Long, ByVal opened As Long) As Long
Public Declare Function HPDF_Outline_SetDestination Lib "libhpdf.dll" (outline As Long, dst As Long) As Long
'/*--------------------------------------------------------------------------*/
'/*----- destination --------------------------------------------------------*/
Public Declare Function HPDF_Page_CreateDestination Lib "libhpdf.dll" (ByVal page As Long) As Long
Public Declare Function HPDF_Destination_SetXYZ Lib "libhpdf.dll" (dst As Long, ByVal Left As Single, ByVal top As Single, ByVal zoom As Single) As Long
Public Declare Function HPDF_Destination_SetFit Lib "libhpdf.dll" (dst As Long) As Long
Public Declare Function HPDF_Destination_SetFitH Lib "libhpdf.dll" (dst As Long, ByVal top As Single) As Long
Public Declare Function HPDF_Destination_SetFitV Lib "libhpdf.dll" (dst As Long, ByVal Left As Single) As Long
Public Declare Function HPDF_Destination_SetFitR Lib "libhpdf.dll" (dst As Long, ByVal Left As Single, ByVal bottom As Single, ByVal Right As Single, ByVal top As Single) As Long
Public Declare Function HPDF_Destination_SetFitB Lib "libhpdf.dll" (dst As Long) As Long
Public Declare Function HPDF_Destination_SetFitBH Lib "libhpdf.dll" (dst As Long, ByVal top As Single) As Long
Public Declare Function HPDF_Destination_SetFitBV Lib "libhpdf.dll" (dst As Long, ByVal Left As Single) As Long
'/*--------------------------------------------------------------------------*/
'/*----- encoder ------------------------------------------------------------*/
Public Declare Function HPDF_GetEncoder Lib "libhpdf.dll" (ByVal pdf As Long, ByVal encoding_name As String) As Long
Public Declare Function HPDF_GetCurrentEncoder Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_SetCurrentEncoder Lib "libhpdf.dll" (ByVal pdf As Long, ByVal encoding_name As String) As Long
Public Declare Function HPDF_Encoder_GetType Lib "libhpdf.dll" (encoder As Long) As Long
Public Declare Function HPDF_Encoder_GetByteType Lib "libhpdf.dll" (encoder As Long, ByVal text As String, ByVal index As Long) As Byte
Public Declare Function HPDF_Encoder_GetUnicode Lib "libhpdf.dll" (encoder As Long, ByVal code As Integer) As Integer
Public Declare Function HPDF_Encoder_GetWritingMode Lib "libhpdf.dll" (encoder As Long) As Long
Public Declare Function HPDF_UseJPEncodings Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_UseKREncodings Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_UseCNSEncodings Lib "libhpdf.dll" (ByVal pdf As Long) As Long
Public Declare Function HPDF_UseCNTEncodings Lib "libhpdf.dll" (ByVal pdf As Long) As Long
'/*--------------------------------------------------------------------------*/
'/*----- annotation ---------------------------------------------------------*/
Public Declare Function HPDF_Page_CreateTextAnnot Lib "libhpdf.dll" (ByVal page As Long, rect As HPDF_Rect, ByVal text As String, ByVal encoder As Long) As Long
Public Declare Function HPDF_Page_CreateLinkAnnot Lib "libhpdf.dll" (ByVal page As Long, rect As HPDF_Rect, ByVal dst As Long) As Long
Public Declare Function HPDF_Page_CreateURILinkAnnot Lib "libhpdf.dll" (ByVal page As Long, ByVal rect As HPDF_Rect, ByVal uri As String) As Long
Public Declare Function HPDF_LinkAnnot_SetHighlightMode Lib "libhpdf.dll" (ByVal annot As Long, mode As HPDF_AnnotHighlightMode) As Long
Public Declare Function HPDF_LinkAnnot_SetBorderStyle Lib "libhpdf.dll" (ByVal annot As Long, ByVal Width As Single, ByVal dash_on As Integer, ByVal dash_off As Integer) As Long
Public Declare Function HPDF_TextAnnot_SetIcon Lib "libhpdf.dll" (ByVal annot As Long, ByVal icon As HPDF_AnnotIcon) As Long
Public Declare Function HPDF_TextAnnot_SetOpened Lib "libhpdf.dll" (ByVal annot As Long, ByVal opened As Long) As Long
'/*--------------------------------------------------------------------------*/
'/*----- image data ---------------------------------------------------------*/
Public Declare Function HPDF_LoadPngImageFromFile Lib "libhpdf.dll" (ByVal pdf As Long, ByVal filename As String) As Long
Public Declare Function HPDF_LoadPngImageFromFile2 Lib "libhpdf.dll" (ByVal pdf As Long, ByVal filename As String) As Long
Public Declare Function HPDF_LoadJpegImageFromFile Lib "libhpdf.dll" (ByVal pdf As Long, ByVal filename As String) As Long
Public Declare Function HPDF_LoadRawImageFromFile Lib "libhpdf.dll" (ByVal pdf As Long, ByVal filename As String, ByVal Width As Long, ByVal height As Long, ByVal color_space As HPDF_ColorSpace) As Long
Public Declare Function HPDF_LoadRawImageFromMem Lib "libhpdf.dll" (ByVal pdf As Long, ByRef buf As Byte, ByVal Width As Long, ByVal height As Long, ByVal color_space As HPDF_ColorSpace, ByVal bits_per_component As Long) As Long
Public Declare Function HPDF_Image_GetSize Lib "libhpdf.dll" (ByVal image As Long) As Long
Public Declare Function HPDF_Image_GetWidth Lib "libhpdf.dll" (ByVal image As Long) As Long
Public Declare Function HPDF_Image_GetHeight Lib "libhpdf.dll" (ByVal image As Long) As Long
Public Declare Function HPDF_Image_GetBitsPerComponent Lib "libhpdf.dll" (ByVal image As Long) As Long
Public Declare Function HPDF_Image_GetColorSpace Lib "libhpdf.dll" (ByVal image As Long) As String
Public Declare Function HPDF_Image_SetColorMask Lib "libhpdf.dll" (ByVal image As Long, ByVal rmin As Long, ByVal rmax As Long, ByVal gmin As Long, ByVal gmax As Long, ByVal bmin As Long, ByVal bmax As Long) As Long
Public Declare Function HPDF_Image_SetMaskImage Lib "libhpdf.dll" (ByVal image As Long, ByVal mask_image As Long) As Long
'/*--------------------------------------------------------------------------*/
'/*----- info dictionary ----------------------------------------------------*/
Public Declare Function HPDF_SetInfoAttr Lib "libhpdf.dll" (ByVal pdf As Long, type0 As HPDF_InfoType, ByRef value As Long) As Long
Public Declare Function HPDF_GetInfoAttr Lib "libhpdf.dll" (ByVal pdf As Long, type0 As HPDF_InfoType) As String
Public Declare Function HPDF_SetInfoDateAttr Lib "libhpdf.dll" (ByVal pdf As Long, type0 As HPDF_InfoType, value As HPDF_Date) As Long
'/*--------------------------------------------------------------------------*/
'/*----- encryption ---------------------------------------------------------*/
Public Declare Function HPDF_SetPassword Lib "libhpdf.dll" (ByVal pdf As Long, ByVal owner_passwd As String, ByVal user_passwd As String) As Long
Public Declare Function HPDF_SetPermission Lib "libhpdf.dll" (ByVal pdf As Long, ByVal permission As Long) As Long
Public Declare Function HPDF_SetEncryptionMode Lib "libhpdf.dll" (ByVal pdf As Long, mode As HPDF_EncryptMode, ByVal key_len As Long) As Long
'/*--------------------------------------------------------------------------*/
'/*----- compression --------------------------------------------------------*/
Public Declare Function HPDF_SetCompressionMode Lib "libhpdf.dll" (ByVal pdf As Long, ByVal mode As Long) As Long
'/*--------------------------------------------------------------------------*/
'/*--------------------------------------------------------------------------*/
Public Declare Function HPDF_Font_GetFontName Lib "libhpdf.dll" (ByVal font As Long) As String
Public Declare Function HPDF_Font_GetEncodingName Lib "libhpdf.dll" (ByVal font As Long) As String
Public Declare Function HPDF_Font_GetUnicodeWidth Lib "libhpdf.dll" (ByVal font As Long, code As Long) As Long
Public Declare Function HPDF_Font_GetBBox Lib "libhpdf.dll" (ByVal font As Long) As HPDF_Box
Public Declare Function HPDF_Font_GetAscent Lib "libhpdf.dll" (ByVal font As Long) As Long
Public Declare Function HPDF_Font_GetDescent Lib "libhpdf.dll" (ByVal font As Long) As Long
Public Declare Function HPDF_Font_GetXHeight Lib "libhpdf.dll" (ByVal font As Long) As Long
Public Declare Function HPDF_Font_GetCapHeight Lib "libhpdf.dll" (ByVal font As Long) As Long
Public Declare Function HPDF_Font_TextWidth Lib "libhpdf.dll" (ByVal font As Long, ByVal text As Byte, ByRef length As Long) As Single
Public Declare Function HPDF_Font_MeasureText Lib "libhpdf.dll" (ByVal font As Long, ByVal text As Byte, ByVal length As Long, ByVal Width As Single, ByVal font_size As Single, ByVal char_space As Single, ByVal word_space As Single, ByVal wordwrap As Long, ByVal real_width As Single) As Long
'/*--------------------------------------------------------------------------*/
'/*--------------------------------------------------------------------------*/
Public Declare Function HPDF_Page_TextWidth Lib "libhpdf.dll" (ByVal page As Long, ByVal text As String) As Single
Public Declare Function HPDF_Page_MeasureText Lib "libhpdf.dll" (ByVal page As Long, ByVal text As String, ByVal Width As Single, ByVal wordwrap As Long, ByVal real_width As Single) As Long
Public Declare Function HPDF_Page_GetWidth Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetHeight Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetGMode Lib "libhpdf.dll" (ByVal page As Long) As Integer
Public Declare Function HPDF_Page_GetCurrentPos Lib "libhpdf.dll" (ByVal page As Long) As HPDF_Point
Public Declare Function HPDF_Page_GetCurrentTextPos Lib "libhpdf.dll" (ByVal page As Long) As HPDF_Point
Public Declare Function HPDF_Page_GetCurrentFont Lib "libhpdf.dll" (ByVal page As Long) As Long
Public Declare Function HPDF_Page_GetCurrentFontSize Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetTransMatrix Lib "libhpdf.dll" (ByVal page As Long) As HPDF_TransMatrix
Public Declare Function HPDF_Page_GetLineWidth Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetLineCap Lib "libhpdf.dll" (ByVal page As Long) As HPDF_LineCap
Public Declare Function HPDF_Page_GetLineJoin Lib "libhpdf.dll" (ByVal page As Long) As HPDF_LineJoin
Public Declare Function HPDF_Page_GetMiterLimit Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetDash Lib "libhpdf.dll" (ByVal page As Long) As HPDF_DashMode
Public Declare Function HPDF_Page_GetFlat Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetCharSpace Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetWordSpace Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetHorizontalScalling Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetTextLeading Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetTextRenderingMode Lib "libhpdf.dll" (ByVal page As Long) As HPDF_TextRenderingMode
'/* This function is obsolete. Use HPDF_Page_GetTextRise.  */
Public Declare Function HPDF_Page_GetTextRaise Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetTextRise Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetRGBFill Lib "libhpdf.dll" (ByVal page As Long) As HPDF_RGBColor
Public Declare Function HPDF_Pageg_GetRGBStroke Lib "libhpdf.dll" (ByVal page As Long) As HPDF_RGBColor
Public Declare Function HPDF_Page_GetCMYKFill Lib "libhpdf.dll" (ByVal page As Long) As HPDF_CMYKColor
Public Declare Function HPDF_Page_GetCMYKStroke Lib "libhpdf.dll" (ByVal page As Long) As HPDF_CMYKColor
Public Declare Function HPDF_Page_GetGrayFill Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetGrayStroke Lib "libhpdf.dll" (ByVal page As Long) As Single
Public Declare Function HPDF_Page_GetStrokingColorSpace Lib "libhpdf.dll" (ByVal page As Long) As HPDF_ColorSpace
Public Declare Function HPDF_Page_GetFillingColorSpace Lib "libhpdf.dll" (ByVal page As Long) As HPDF_ColorSpace
Public Declare Function HPDF_Page_GetTextMatrix Lib "libhpdf.dll" (ByVal page As Long) As HPDF_TransMatrix
Public Declare Function HPDF_Page_GetGStateDepth Lib "libhpdf.dll" (ByVal page As Long) As Long
'/*--------------------------------------------------------------------------*/
'/*----- GRAPHICS OPERATORS -------------------------------------------------*/
'/*--- General graphics state ---------------------------------------------*/
'/* w */
Public Declare Function HPDF_Page_SetLineWidth Lib "libhpdf.dll" (ByVal page As Long, ByVal line_width As Single) As Long
'/* J */
Public Declare Function HPDF_Page_SetLineCap Lib "libhpdf.dll" (ByVal page As Long, ByVal line_cap As HPDF_LineCap) As Long
'/* j */
Public Declare Function HPDF_Page_SetLineJoin Lib "libhpdf.dll" (ByVal page As Long, ByVal line_join As HPDF_LineJoin) As Long
'/* M */
Public Declare Function HPDF_Page_SetMiterLimit Lib "libhpdf.dll" (ByVal page As Long, ByVal miter_limit As Single) As Long
'/* d */
Public Declare Function HPDF_Page_SetDash Lib "libhpdf.dll" (ByVal page As Long, ByRef dash_ptn As Long, ByVal num_param As Long, ByVal phase As Long) As Long
'/* ri --not implemented yet */
'/* i */
Public Declare Function HPDF_Page_SetFlat Lib "libhpdf.dll" (ByVal page As Long, ByVal flatness As Single) As Long
'/* gs --not implemented yet */
'/*--- Special graphic state operator --------------------------------------*/
'/* q */
Public Declare Function HPDF_Page_GSave Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* Q */
Public Declare Function HPDF_Page_GRestore Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* cm */
Public Declare Function HPDF_Page_Concat Lib "libhpdf.dll" (ByVal page As Long, ByVal a As Single, ByVal b As Single, ByVal c As Single, ByVal d As Single, ByVal x As Single, ByVal y As Single) As Long
'/*--- Path construction operator ------------------------------------------*/
'/* m */
Public Declare Function HPDF_Page_MoveTo Lib "libhpdf.dll" (ByVal page As Long, ByVal x As Single, ByVal y As Single) As Long
'/* l */
Public Declare Function HPDF_Page_LineTo Lib "libhpdf.dll" (ByVal page As Long, ByVal x As Single, ByVal y As Single) As Long
'/* c */
Public Declare Function HPDF_Page_CurveTo Lib "libhpdf.dll" (ByVal page As Long, ByVal x1 As Single, ByVal y1 As Single, ByVal x2 As Single, ByVal y2 As Single, ByVal x3 As Single, ByVal y3 As Single) As Long
'/* v */
Public Declare Function HPDF_Page_CurveTo2 Lib "libhpdf.dll" (ByVal page As Long, ByVal x2 As Single, ByVal y2 As Single, ByVal x3 As Single, ByVal y3 As Single) As Long
'/* y */
Public Declare Function HPDF_Page_CurveTo3 Lib "libhpdf.dll" (ByVal page As Long, ByVal x1 As Single, ByVal y1 As Single, ByVal x3 As Single, ByVal y3 As Single) As Long
'/* h */
Public Declare Function HPDF_Page_ClosePath Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* re */
Public Declare Function HPDF_Page_Rectangle Lib "libhpdf.dll" (ByVal page As Long, ByVal x As Single, ByVal y As Single, ByVal Width As Single, ByVal height As Single) As Long
'/*--- Path painting operator ---------------------------------------------*/
'/* S */
Public Declare Function HPDF_Page_Stroke Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* s */
Public Declare Function HPDF_Page_ClosePathStroke Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* f */
Public Declare Function HPDF_Page_Fill Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* f* */
Public Declare Function HPDF_Page_Eofill Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* B */
Public Declare Function HPDF_Page_FillStroke Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* B* */
Public Declare Function HPDF_Page_EofillStroke Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* b */
Public Declare Function HPDF_Page_ClosePathFillStroke Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* b* */
Public Declare Function HPDF_Page_ClosePathEofillStroke Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* n */
Public Declare Function HPDF_Page_EndPath Lib "libhpdf.dll" (ByVal page As Long) As Long
'/*--- Clipping paths operator --------------------------------------------*/
'/* W */
Public Declare Function HPDF_Page_Clip Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* W* */
Public Declare Function HPDF_Page_Eoclip Lib "libhpdf.dll" (ByVal page As Long) As Long
'/*--- Text object operator -----------------------------------------------*/
'/* BT */
Public Declare Function HPDF_Page_BeginText Lib "libhpdf.dll" (ByVal page As Long) As Long
'/* ET */
Public Declare Function HPDF_Page_EndText Lib "libhpdf.dll" (ByVal page As Long) As Long
'/*--- Text state ---------------------------------------------------------*/
'/* Tc */
Public Declare Function HPDF_Page_SetCharSpace Lib "libhpdf.dll" (ByVal page As Long, ByVal value As Single) As Long
'/* Tw */
Public Declare Function HPDF_Page_SetWordSpace Lib "libhpdf.dll" (ByVal page As Long, ByVal value As Single) As Long
'/* Tz */
Public Declare Function HPDF_Page_SetHorizontalScalling Lib "libhpdf.dll" (ByVal page As Long, ByVal value As Single) As Long
'/* TL */
Public Declare Function HPDF_Page_SetTextLeading Lib "libhpdf.dll" (ByVal page As Long, ByVal value As Single) As Long
'/* Tf */
Public Declare Function HPDF_Page_SetFontAndSize Lib "libhpdf.dll" (ByVal page As Long, ByVal font As Long, ByVal Size As Single) As Long
'/* Tr */
Public Declare Function HPDF_Page_SetTextRenderingMode Lib "libhpdf.dll" (ByVal page As Long, ByVal mode As HPDF_TextRenderingMode) As Long
'/* Ts */
Public Declare Function HPDF_Page_SetTextRise Lib "libhpdf.dll" (ByVal page As Long, ByVal value As Single) As Long
'/* This function is obsolete. Use HPDF_Page_SetTextRise.  */
Public Declare Function HPDF_Page_SetTextRaise Lib "libhpdf.dll" (ByVal page As Long, ByVal value As Single) As Long
'/*--- Text positioning ---------------------------------------------------*/
'/* Td */
Public Declare Function HPDF_Page_MoveTextPos Lib "libhpdf.dll" (ByVal page As Long, ByVal x As Single, ByVal y As Single) As Long
'/* TD */
Public Declare Function HPDF_Page_MoveTextPos2 Lib "libhpdf.dll" (ByVal page As Long, ByVal x As Single, ByVal y As Single) As Long
'/* Tm */
Public Declare Function HPDF_Page_SetTextMatrix Lib "libhpdf.dll" (ByVal page As Long, ByVal a As Single, ByVal b As Single, ByVal c As Single, ByVal d As Single, ByVal x As Single, ByVal y As Single) As Long
'/* T* */
Public Declare Function HPDF_Page_MoveToNextLine Lib "libhpdf.dll" (ByVal page As Long) As Long
'/*--- Text showing -------------------------------------------------------*/
'/* Tj */
Public Declare Function HPDF_Page_ShowText Lib "libhpdf.dll" (ByVal page As Long, ByVal text As String) As Long
'/* TJ */
'/* ' */
Public Declare Function HPDF_Page_ShowTextNextLine Lib "libhpdf.dll" (ByVal page As Long, ByVal text As String) As Long
'/* " */
Public Declare Function HPDF_Page_ShowTextNextLineEx Lib "libhpdf.dll" (ByVal page As Long, ByVal word_space As Single, ByVal char_space As Single, ByVal text As String) As Long
'/*--- Color showing ------------------------------------------------------*/

'/* cs --not implemented yet */
'/* CS --not implemented yet */
'/* sc --not implemented yet */
'/* scn --not implemented yet */
'/* SC --not implemented yet */
'/* SCN --not implemented yet */

'/* g */
Public Declare Function HPDF_Page_SetGrayFill Lib "libhpdf.dll" (ByVal page As Long, ByVal gray As Single) As Long
'/* G */
Public Declare Function HPDF_Page_SetGrayStroke Lib "libhpdf.dll" (ByVal page As Long, ByVal gray As Single) As Long
'/* rg */
Public Declare Function HPDF_Page_SetRGBFill Lib "libhpdf.dll" (ByVal page As Long, ByVal r As Single, ByVal g As Single, ByVal b As Single) As Long
'/* RG */
Public Declare Function HPDF_Page_SetRGBStroke Lib "libhpdf.dll" (ByVal page As Long, ByVal r As Single, ByVal g As Single, ByVal b As Single) As Long
'/* k */
Public Declare Function HPDF_Page_SetCMYKFill Lib "libhpdf.dll" (ByVal page As Long, ByVal c As Single, ByVal m As Single, ByVal y As Single, ByVal k As Single) As Long
'/* K */
Public Declare Function HPDF_Page_SetCMYKStroke Lib "libhpdf.dll" (ByVal page As Long, ByVal c As Single, ByVal m As Single, ByVal y As Single, ByVal k As Single) As Long
'/*--- Shading patterns ---------------------------------------------------*/
'/* sh --not implemented yet */
'/*--- In-line images -----------------------------------------------------*/
'/* BI --not implemented yet */
'/* ID --not implemented yet */
'/* EI --not implemented yet */
'/*--- XObjects -----------------------------------------------------------*/
'/* Do */
Public Declare Function HPDF_Page_ExecuteXObject Lib "libhpdf.dll" (ByVal page As Long, obj As Long) As Long
'/*--- Marked content -----------------------------------------------------*/
'/* BMC --not implemented yet */
'/* BDC --not implemented yet */
'/* EMC --not implemented yet */
'/* MP --not implemented yet */
'/* DP --not implemented yet */
'/*--- Compatibility ------------------------------------------------------*/
'/* BX --not implemented yet */
'/* EX --not implemented yet */
Public Declare Function HPDF_Page_DrawImage Lib "libhpdf.dll" (ByVal page As Long, ByVal image As Long, ByVal x As Single, ByVal y As Single, ByVal Width As Single, ByVal height As Single) As Long
Public Declare Function HPDF_Page_Circle Lib "libhpdf.dll" (ByVal page As Long, ByVal x As Single, ByVal y As Single, ByVal ray As Single) As Long
Public Declare Function HPDF_Page_Ellipse Lib "libhpdf.dll" (ByVal page As Long, ByVal x As Single, ByVal y As Single, ByVal xray As Single, 
 ByVal yray As Single) As Long
Public Declare Function HPDF_Page_Arc Lib "libhpdf.dll" (ByVal page As Long, ByVal x As Single, ByVal y As Single, ByVal ray As Single, ByVal ang1 As Single, ByVal ang2 As Single) As Long
Public Declare Function HPDF_Page_TextOut Lib "libhpdf.dll" (ByVal page As Long, ByVal xpos As Single, ByVal ypos As Single, ByVal text As String) As Long
Public Declare Function HPDF_Page_TextRect Lib "libhpdf.dll" (ByVal page As Long, ByVal Left As Single, ByVal top As Single, ByVal Right As Single, ByVal bottom As Single, ByVal text As String, ByVal align As Long, ByRef length As Long) As Long


Public Sub error_handler(ByVal error_no As Long, ByVal detail_no As Long, ByVal user_data As String)
    Debug.Print "ERROR: error_no=" & error_no & ", detail_no="; detail_no
    'longjmp(env, 1)
End Sub

