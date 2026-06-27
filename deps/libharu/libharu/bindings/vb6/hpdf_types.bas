Attribute VB_Name = "Module3"
''/*
' * << Haru Free PDF Library 2.0.3 >> -- hpdf_types.h
' *
' * URL http://libharu.org/
' *
' * Copyright (c) 1999-2006 Takeshi Kanno
' *
' * Permission to use copy modify distribute and sell this software
' * and its documentation for any purpose is hereby granted without fee
' * provided that the above copyright notice appear in all copies and
' * that both that copyright notice and this permission notice appear
' * in supporting documentation.
' * It is provided "as is" without express or implied warranty.
' *
' */


''/*----------------------------------------------------------------------------*/
''/*----- type definition ------------------------------------------------------*/


'/*  native OS integer types */
'typedef  signed int          HPDF_INT
'typedef  unsigned int        HPDF_UNIT


'/*  32bit integer types
' */
'typedef  signed int          HPDF_INT32
'typedef  unsigned int        HPDF_UNIT32


'/*  16bit integer types
' */
'typedef  signed short        HPDF_INT16
'typedef  unsigned short      HPDF_UNIT16


'/*  8bit integer types
' */
'typedef  signed char         HPDF_INT8
'typedef  unsigned char       HPDF_UNIT8


'/*  8bit binary types
' */
'typedef  unsigned char       HPDF_BYTE


'/*  float type (32bit IEEE754)
' */
'typedef  float               HPDF_REAL


'/*  double type (64bit IEEE754)
' */
'typedef  double              HPDF_DOUBLE


'/*  boolean type (0: False !0: True)
' */
'typedef  signed int          HPDF_BOOL


'/*  error-no type (32bit unsigned integer)
' */
'typedef  unsigned long       HPDF_STATUS


'/*  character-code type (16bit)
' */
'typedef  long16         HPDF_CID
'typedef  long16         HPDF_UNICODE


'/*  HPDF_Point struct
' */
Public Type HPDF_Point
    x As Single
    y As Single
End Type

Public Type HPDF_Rect
    Left        As Single
    bottom      As Single
    Right       As Single
    top         As Single
End Type

Public Type HPDF_Box
    Left        As Single
    bottom      As Single
    Right       As Single
    top         As Single
End Type

'/* HPDF_Date struct
' */
Public Type HPDF_Date
    Year        As Long
    Month       As Long
    Day         As Long
    Hour        As Long
    minutes     As Long
    seconds     As Long
    ind         As Byte
    off_hour    As Long
    off_minutes As Long
End Type


Public Enum HPDF_InfoType
    '/* date-time type parameters */
    HPDF_INFO_CREATION_DATE = 0
    HPDF_INFO_MOD_DATE = 1

    '/* string type parameters */
    HPDF_INFO_AUTHOR = 2
    HPDF_INFO_CREATOR = 3
    HPDF_INFO_PRODUCER = 4
    HPDF_INFO_TITLE = 5
    HPDF_INFO_SUBJECT = 6
    HPDF_INFO_KEYWORDS = 7
    HPDF_INFO_EOF = 8
End Enum


Public Enum HPDF_EncryptMode
    HPDF_ENCRYPT_R2 = 2
    HPDF_ENCRYPT_R3 = 3
End Enum


#If 0 Then
typedef void
(HPDF_STDCALL *HPDF_Error_Handler)  (long   error_no
                                     long   detail_no
                                     void         *user_data)

typedef void*
(HPDF_STDCALL *HPDF_Alloc_Func)  (long  size)


typedef void
(HPDF_STDCALL *HPDF_Free_Func)  (void  *aptr)
#End If

'/*---------------------------------------------------------------------------*/
'/*------ text width struct --------------------------------------------------*/
Public Type HPDF_TextWidth
    numchars    As Long
    '/* don't use this value (it may be change in the feature).
    '   use numspace as alternated. */
    numwords    As Long
    Width       As Long
    numspace    As Long
End Type


'/*---------------------------------------------------------------------------*/
'/*------ dash mode ----------------------------------------------------------*/
Public Type HPDF_DashMode
    ptn(8)      As Integer
    num_ptn     As Long
    phase       As Long
End Type


'/*---------------------------------------------------------------------------*/
'/*----- HPDF_TransMatrix struct ---------------------------------------------*/
Public Type HPDF_TransMatrix
     a          As Single
     b          As Single
     c          As Single
     d          As Single
     x          As Single
     y          As Single
End Type


'/*---------------------------------------------------------------------------*/
Public Enum HPDF_ColorSpace
    HPDF_CS_DEVICE_GRAY = 0
    HPDF_CS_DEVICE_RGB = 1
    HPDF_CS_DEVICE_CMYK = 2
    HPDF_CS_CAL_GRAY = 3
    HPDF_CS_CAL_RGB = 4
    HPDF_CS_LAB = 5
    HPDF_CS_ICC_BASED = 6
    HPDF_CS_SEPARATION = 7
    HPDF_CS_DEVICE_N = 8
    HPDF_CS_INDEXED = 9
    HPDF_CS_PATTERN = 10
    HPDF_CS_EOF = 11
End Enum

'/*---------------------------------------------------------------------------*/
'/*----- HPDF_RGBColor struct ------------------------------------------------*/
Public Type HPDF_RGBColor
       r        As Single
       g        As Single
       b        As Single
End Type

'/*---------------------------------------------------------------------------*/
'/*----- HPDF_CMYKColor struct -----------------------------------------------*/
Public Type HPDF_CMYKColor
       c      As Single
       m      As Single
       y      As Single
       k      As Single
End Type

'/*---------------------------------------------------------------------------*/
'/*------ The line cap style -------------------------------------------------*/
Public Enum HPDF_LineCap
    HPDF_BUTT_END = 0
    HPDF_ROUND_END = 1
    HPDF_PROJECTING_SQUARE_END = 2
    HPDF_LINECAP_EOF = 3
End Enum

'/*----------------------------------------------------------------------------*/
'/*------ The line join style -------------------------------------------------*/
Public Enum HPDF_LineJoin
    HPDF_MITER_JOIN = 0
    HPDF_ROUND_JOIN = 1
    HPDF_BEVEL_JOIN = 2
    HPDF_LINEJOIN_EOF = 3
End Enum

'/*----------------------------------------------------------------------------*/
'/*------ The text rendering mode ---------------------------------------------*/
Public Enum HPDF_TextRenderingMode
    HPDF_FILL = 0
    HPDF_STROKE = 1
    HPDF_FILL_THEN_STROKE = 2
    HPDF_INVISIBLE = 3
    HPDF_FILL_CLIPPING = 4
    HPDF_STROKE_CLIPPING = 5
    HPDF_FILL_STROKE_CLIPPING = 6
    HPDF_CLIPPING = 7
    HPDF_RENDERING_MODE_EOF = 8
End Enum


Public Enum HPDF_WritingMode
    HPDF_WMODE_HORIZONTAL = 0
    HPDF_WMODE_VERTICAL = 1
    HPDF_WMODE_EOF = 2
End Enum


Public Enum HPDF_PageLayout
    HPDF_PAGE_LAYOUT_SINGLE = 0
    HPDF_PAGE_LAYOUT_ONE_COLUMN = 1
    HPDF_PAGE_LAYOUT_TWO_COLUMN_LEFT = 2
    HPDF_PAGE_LAYOUT_TWO_COLUMN_RIGHT = 3
    HPDF_PAGE_LAYOUT_EOF = 4
End Enum


Public Enum HPDF_PageMode
    HPDF_PAGE_MODE_USE_NONE = 0
    HPDF_PAGE_MODE_USE_OUTLINE = 1
    HPDF_PAGE_MODE_USE_THUMBS = 2
    HPDF_PAGE_MODE_FULL_SCREEN = 3
'/*  HPDF_PAGE_MODE_USE_OC              =4
    HPDF_PAGE_MODE_USE_ATTACHMENTS = 4
' */
    HPDF_PAGE_MODE_EOF
End Enum


Public Enum HPDF_PageNumStyle
    HPDF_PAGE_NUM_STYLE_DECIMAL = 0
    HPDF_PAGE_NUM_STYLE_UPPER_ROMAN = 1
    HPDF_PAGE_NUM_STYLE_LOWER_ROMAN = 2
    HPDF_PAGE_NUM_STYLE_UPPER_LETTERS = 3
    HPDF_PAGE_NUM_STYLE_LOWER_LETTERS = 4
    HPDF_PAGE_NUM_STYLE_EOF = 5
End Enum


Public Enum HPDF_DestinationType
    HPDF_XYZ = 0
    HPDF_FIT = 1
    HPDF_FIT_H = 2
    HPDF_FIT_V = 3
    HPDF_FIT_R = 4
    HPDF_FIT_B = 5
    HPDF_FIT_BH = 6
    HPDF_FIT_BV = 7
    HPDF_DST_EOF = 8
End Enum


Public Enum HPDF_AnnotType
    HPDF_ANNOT_TEXT_NOTES = 1
    HPDF_ANNOT_LINK = 2
    HPDF_ANNOT_SOUND = 3
    HPDF_ANNOT_FREE_TEXT = 4
    HPDF_ANNOT_STAMP = 5
    HPDF_ANNOT_SQUARE = 6
    HPDF_ANNOT_CIRCLE = 7
    HPDF_ANNOT_STRIKE_OUT = 8
    HPDF_ANNOT_HIGHTLIGHT = 9
    HPDF_ANNOT_UNDERLINE = 10
    HPDF_ANNOT_INK = 11
    HPDF_ANNOT_FILE_ATTACHMENT = 12
    HPDF_ANNOT_POPUP = 13
End Enum


Public Enum HPDF_AnnotFlgs
    HPDF_ANNOT_INVISIBLE = 1
    HPDF_ANNOT_HIDDEN = 2
    HPDF_ANNOT_PRINT = 3
    HPDF_ANNOT_NOZOOM = 4
    HPDF_ANNOT_NOROTATE = 5
    HPDF_ANNOT_NOVIEW = 6
    HPDF_ANNOT_READONLY = 7
End Enum


Public Enum HPDF_AnnotHighlightMode
    HPDF_ANNOT_NO_HIGHTLIGHT = 0
    HPDF_ANNOT_INVERT_BOX = 1
    HPDF_ANNOT_INVERT_BORDER = 2
    HPDF_ANNOT_DOWN_APPEARANCE = 3
    HPDF_ANNOT_HIGHTLIGHT_MODE_EOF = 4
End Enum


Public Enum HPDF_AnnotIcon
    HPDF_ANNOT_ICON_COMMENT = 0
    HPDF_ANNOT_ICON_KEY = 1
    HPDF_ANNOT_ICON_NOTE = 2
    HPDF_ANNOT_ICON_HELP = 3
    HPDF_ANNOT_ICON_NEW_PARAGRAPH = 4
    HPDF_ANNOT_ICON_PARAGRAPH = 5
    HPDF_ANNOT_ICON_INSERT = 6
    HPDF_ANNOT_ICON_EOF = 7
End Enum



'/*----------------------------------------------------------------------------*/
'/*------ border stype --------------------------------------------------------*/
Public Enum HPDF_BSSubtype
    HPDF_BS_SOLID = 1
    HPDF_BS_DASHED = 2
    HPDF_BS_BEVELED = 3
    HPDF_BS_INSET = 4
    HPDF_BS_UNDERLINED = 5
End Enum


Public Enum HPDF_PageSizes
    HPDF_PAGE_SIZE_LETTER = 0
    HPDF_PAGE_SIZE_LEGAL = 1
    HPDF_PAGE_SIZE_A3 = 2
    HPDF_PAGE_SIZE_A4 = 3
    HPDF_PAGE_SIZE_A5 = 4
    HPDF_PAGE_SIZE_B4 = 5
    HPDF_PAGE_SIZE_B5 = 6
    HPDF_PAGE_SIZE_EXECUTIVE = 7
    HPDF_PAGE_SIZE_US4x6 = 8
    HPDF_PAGE_SIZE_US4x8 = 9
    HPDF_PAGE_SIZE_US5x7 = 10
    HPDF_PAGE_SIZE_COMM10 = 11
    HPDF_PAGE_SIZE_EOF = 12
End Enum


Public Enum HPDF_PageDirection
    HPDF_PAGE_PORTRAIT = 0
    HPDF_PAGE_LANDSCAPE = 1
End Enum


Public Enum HPDF_EncoderType
    HPDF_ENCODER_TYPE_SINGLE_BYTE = 1
    HPDF_ENCODER_TYPE_DOUBLE_BYTE = 2
    HPDF_ENCODER_TYPE_UNINITIALIZED = 3
    HPDF_ENCODER_UNKNOWN = 4
End Enum


Public Enum HPDF_ByteType
    HPDF_BYTE_TYPE_SINGLE = 0
    HPDF_BYTE_TYPE_LEAD = 1
    HPDF_BYTE_TYPE_TRAIL = 2
    HPDF_BYTE_TYPE_UNKNOWN = 3
End Enum


Public Enum HPDF_TextAlignment
    HPDF_TALIGN_LEFT = 0
    HPDF_TALIGN_RIGHT = 1
    HPDF_TALIGN_CENTER = 2
    HPDF_TALIGN_JUSTIFY = 3
End Enum
