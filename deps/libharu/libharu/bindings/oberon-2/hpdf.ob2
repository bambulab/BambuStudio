<*+CHANGESYM*>
<*+M2EXTENSIONS*>
<*+O2EXTENSIONS*>
(*
#
#  URL http://libharu.org/
#
#  Copyright 2000-2006 (c) Takeshi Kanno
#  Copyright 2007-2009 (c) Antony Dovgal et al.
#
#  Copyright 2012 (c) Dmitry Solomennikov
#  Oberon-2 language binding to libhpdf.dll
#
*)

MODULE hpdf;

IMPORT 
  Windows, SYSTEM;

(*
  NOTE:

  if you want to use Haru with Kylix/FreePascal on Linux,
  change "libhpdf.dll" to "libhpdf.so" and change "stdcall" to "cdecl"
  (This file and hpdf_types.pas)

*)

TYPE
  HPDF_HANDLE      * = Windows.PVOID;
  HPDF_Doc         * = HPDF_HANDLE;
  HPDF_Page        * = HPDF_HANDLE;
  HPDF_Pages       * = HPDF_HANDLE;
  HPDF_Stream      * = HPDF_HANDLE;
  HPDF_Image       * = HPDF_HANDLE;
  HPDF_Font        * = HPDF_HANDLE;
  HPDF_Outline     * = HPDF_HANDLE;
  HPDF_Encoder     * = HPDF_HANDLE;
  HPDF_Destination * = HPDF_HANDLE;
  HPDF_XObject     * = HPDF_HANDLE;
  HPDF_Annotation  * = HPDF_HANDLE;
  HPDF_ExtGState   * = HPDF_HANDLE;
--  HPDF_CHAR        = Windows.PSTR;
  --HPDF_STATUS      = Windows.UINT;

(*----------------------------------------------------------------------------*)
(*----- type definition ------------------------------------------------------*)


(*  native OS integer types *)
  HPDF_INT   * = Windows.LONG;
  HPDF_UINT  * = Windows.UINT;
  HPDF_PUINT * = Windows.PUINT;

(*
  32bit integer types
*)
  HPDF_INT32 * = Windows.LONG;
  HPDF_UINT32 * = Windows.ULONG;
  HPDF_PUINT32 * = Windows.PULONG;


(*
  16bit integer types
 *)
  HPDF_INT16 * = Windows.SHORT;
  HPDF_UINT16 * = Windows.USHORT;
  HPDF_PUINT16 * = POINTER TO ARRAY OF HPDF_UINT16;



(*
  8bit integer types
*)
  HPDF_INT8  * = Windows.INT8;
  HPDF_UINT8 * = Windows.CARD8;


(*
  8bit character types
*)
  HPDF_CHAR * = Windows.PSTR;

(*
  8bit binary types
*)
  HPDF_BYTE * = Windows.BYTE;
  HPDF_PBYTE * = Windows.PBYTE;


(*
  float type (32bit IEEE754)
*)
  HPDF_REAL * = Windows.FLOAT;
--  HPDF_PREAL * = ^HPDF_REAL;


(*  
  double type (64bit IEEE754)
 *)
  HPDF_DOUBLE * = LONGREAL;


(*  
  boolean type (0: False, 1: True)
 *)
  HPDF_BOOL * = Windows.BOOL;


(*  
  error-no type (32bit unsigned integer)
 *)
  HPDF_STATUS * = Windows.UINT;


(*  
  character-code type (16bit)
 *)
  HPDF_CID * = Windows.WCHAR;
  HPDF_UNICODE * = Windows.WCHAR;

(*  
  null terminated character *)
  HPDF_PCHAR * = Windows.PSTR;


(*  
  HPDF_Box struct
 *)
  THPDF_Box * = RECORD
    left: HPDF_REAL;
    bottom: HPDF_REAL;
    right: HPDF_REAL;
    top: HPDF_REAL;
  END;


(*  
  HPDF_Point struct
 *)
  PHPDF_Point * = POINTER ["StdCall"] TO THPDF_Point;
  THPDF_Point * = RECORD
    x: HPDF_REAL;
    y: HPDF_REAL;
  END;


(*  
  HPDF_Rect struct
 *)
  THPDF_Rect * = RECORD
    left: HPDF_REAL;
    bottom: HPDF_REAL;
    right: HPDF_REAL;
    top: HPDF_REAL;
  END;


(* 
  HPDF_Date struct
 *)
  THPDF_Date * = RECORD
    year: HPDF_INT;
    month: HPDF_INT;
    day: HPDF_INT;
    hour: HPDF_INT;
    minutes: HPDF_INT;
    seconds: HPDF_INT;
    ind: HPDF_CHAR;
    off_hour: HPDF_INT;
    off_minutes: HPDF_INT;
  END;

(*---------------------------------------------------------------------------*)
(*------ text width struct --------------------------------------------------*)

  THPDF_TextWidth * = RECORD
    numchars: HPDF_UINT;
    numwords: HPDF_UINT;   (* don't use this value. *)
    width: HPDF_UINT;
    numspace: HPDF_UINT;
  END;

(*---------------------------------------------------------------------------*)
(*------ dash mode ----------------------------------------------------------*)

  THPDF_DashMode * = RECORD ["StdCall"]
    ptn: ARRAY 8 OF HPDF_UINT16;
    num_ptn: HPDF_UINT16;
    phase: HPDF_UINT16;
  END;


(*---------------------------------------------------------------------------*)
(*----- HPDF_TransMatrix struct ---------------------------------------------*)

  THPDF_TransMatrix * = RECORD
    a: HPDF_REAL;
    b: HPDF_REAL;
    c: HPDF_REAL;
    d: HPDF_REAL;
    x: HPDF_REAL;
    y: HPDF_REAL;
  END;

(*---------------------------------------------------------------------------*)
(*----- HPDF_RGBColor struct ------------------------------------------------*)

  THPDF_RGBColor * = RECORD
    r: HPDF_REAL;
    g: HPDF_REAL;
    b: HPDF_REAL;
  END;

(*---------------------------------------------------------------------------*)
(*----- HPDF_CMYKColor struct -----------------------------------------------*)

  THPDF_CMYKColor * = RECORD
    c: HPDF_REAL;
    m: HPDF_REAL;
    y: HPDF_REAL;
    k: HPDF_REAL;
  END;


TYPE
  THPDF_ColorSpace * = Windows.UINT;
CONST
  HPDF_CS_DEVICE_GRAY                * = 0;
  HPDF_CS_DEVICE_RGB                 * = 1;
  HPDF_CS_DEVICE_CMYK                * = 2;
  HPDF_CS_CAL_GRAY                   * = 3;
  HPDF_CS_CAL_RGB                    * = 4;
  HPDF_CS_LAB                        * = 5;
  HPDF_CS_ICC_BASED                  * = 6;
  HPDF_CS_SEPARATION                 * = 7;
  HPDF_CS_DEVICE_N                   * = 8;
  HPDF_CS_INDEXED                    * = 9;
  HPDF_CS_PATTERN                    * = 10;
  HPDF_CS_EOF                        * = 11;

(*----------------------------------------------------------------------------*)
(*------ The line cap style --------------------------------------------------*)
TYPE
  THPDF_LineCap * = Windows.UINT;
CONST
  HPDF_BUTT_END                      * = 0;
  HPDF_ROUND_END                     * = 1;
  HPDF_PROJECTING_SQUARE_END         * = 2;
  HPDF_LINECAP_EOF                   * = 3;

(*----------------------------------------------------------------------------*)
(*------ The line join style -------------------------------------------------*)
TYPE
  THPDF_LineJoin * = Windows.UINT;
CONST
  HPDF_MITER_JOIN                    * = 0;
  HPDF_ROUND_JOIN                    * = 1;
  HPDF_BEVEL_JOIN                    * = 2;
  HPDF_LINEJOIN_EOF                  * = 3;

(*----------------------------------------------------------------------------*)
(*------ The text rendering mode ---------------------------------------------*)
TYPE
  THPDF_TextRenderingMode * = Windows.UINT;
CONST
  HPDF_FILL                          * = 0;
  HPDF_STROKE                        * = 1;
  HPDF_FILL_THEN_STROKE              * = 2;
  HPDF_INVISIBLE                     * = 3;
  HPDF_FILL_CLIPPING                 * = 4;
  HPDF_STROKE_CLIPPING               * = 5;
  HPDF_FILL_STROKE_CLIPPING          * = 6;
  HPDF_CLIPPING                      * = 7;
  HPDF_RENDERING_MODE_EOF            * = 8;

TYPE
  THPDF_WritingMode * = Windows.UINT;
CONST
  HPDF_WMODE_HORIZONTAL              * = 0;
  HPDF_WMODE_VERTICAL                * = 1;
  HPDF_WMODE_EOF                     * = 2;



TYPE
  THPDF_PageLayout * = Windows.UINT;
CONST
  HPDF_PAGE_LAYOUT_SINGLE            * = 0;
  HPDF_PAGE_LAYOUT_ONE_COLUMN        * = 1;
  HPDF_PAGE_LAYOUT_TWO_COLUMN_LEFT   * = 2;
  HPDF_PAGE_LAYOUT_TWO_COLUMN_RIGHT  * = 3;
  HPDF_PAGE_LAYOUT_EOF               * = 4;

TYPE
  THPDF_PageMode= Windows.UINT;
CONST
  HPDF_PAGE_MODE_USE_NONE            * = 0;
  HPDF_PAGE_MODE_USE_OUTLINE         * = 1;
  HPDF_PAGE_MODE_USE_THUMBS          * = 2;
  HPDF_PAGE_MODE_FULL_SCREEN         * = 3;
(*  HPDF_PAGE_MODE_USE_OC              * =4
  HPDF_PAGE_MODE_USE_ATTACHMENTS     * = 4;
' *)
  HPDF_PAGE_MODE_EOF                 * = 4;

TYPE
  THPDF_PageNumStyle= Windows.UINT;
CONST
  HPDF_PAGE_NUM_STYLE_DECIMAL        * = 0;
  HPDF_PAGE_NUM_STYLE_UPPER_ROMAN    * = 1;
  HPDF_PAGE_NUM_STYLE_LOWER_ROMAN    * = 2;
  HPDF_PAGE_NUM_STYLE_UPPER_LETTERS  * = 3;
  HPDF_PAGE_NUM_STYLE_LOWER_LETTERS  * = 4;
  HPDF_PAGE_NUM_STYLE_EOF            * = 5;

TYPE
  THPDF_DestinationType= Windows.UINT;
CONST
  HPDF_XYZ                           * = 0;
  HPDF_FIT                           * = 1;
  HPDF_FIT_H                         * = 2;
  HPDF_FIT_V                         * = 3;
  HPDF_FIT_R                         * = 4;
  HPDF_FIT_B                         * = 5;
  HPDF_FIT_BH                        * = 6;
  HPDF_FIT_BV                        * = 7;
  HPDF_DST_EOF                       * = 8;

TYPE
  THPDF_AnnotType= Windows.UINT;
CONST
  HPDF_ANNOT_TEXT_NOTES              * = 1;
  HPDF_ANNOT_LINK                    * = 2;
  HPDF_ANNOT_SOUND                   * = 3;
  HPDF_ANNOT_FREE_TEXT               * = 4;
  HPDF_ANNOT_STAMP                   * = 5;
  HPDF_ANNOT_SQUARE                  * = 6;
  HPDF_ANNOT_CIRCLE                  * = 7;
  HPDF_ANNOT_STRIKE_OUT              * = 8;
  HPDF_ANNOT_HIGHTLIGHT              * = 9;
  HPDF_ANNOT_UNDERLINE               * = 10;
  HPDF_ANNOT_INK                     * = 11;
  HPDF_ANNOT_FILE_ATTACHMENT         * = 12;
  HPDF_ANNOT_POPUP                   * = 13;

TYPE
  THPDF_AnnotFlgs= Windows.UINT;
CONST
  HPDF_ANNOT_INVISIBLE               * = 1;
  HPDF_ANNOT_HIDDEN                  * = 2;
  HPDF_ANNOT_PRINT                   * = 3;
  HPDF_ANNOT_NOZOOM                  * = 4;
  HPDF_ANNOT_NOROTATE                * = 5;
  HPDF_ANNOT_NOVIEW                  * = 6;
  HPDF_ANNOT_READONLY                * = 7;

TYPE
  THPDF_AnnotHighlightMode= Windows.UINT;
CONST
  HPDF_ANNOT_NO_HIGHTLIGHT           * = 0;
  HPDF_ANNOT_INVERT_BOX              * = 1;
  HPDF_ANNOT_INVERT_BORDER           * = 2;
  HPDF_ANNOT_DOWN_APPEARANCE         * = 3;
  HPDF_ANNOT_HIGHTLIGHT_MODE_EOF     * = 4;

TYPE
  THPDF_AnnotIcon= Windows.UINT;
CONST
  HPDF_ANNOT_ICON_COMMENT            * = 0;
  HPDF_ANNOT_ICON_KEY                * = 1;
  HPDF_ANNOT_ICON_NOTE               * = 2;
  HPDF_ANNOT_ICON_HELP               * = 3;
  HPDF_ANNOT_ICON_NEW_PARAGRAPH      * = 4;
  HPDF_ANNOT_ICON_PARAGRAPH          * = 5;
  HPDF_ANNOT_ICON_INSERT             * = 6;
  HPDF_ANNOT_ICON_EOF                * = 7;

(*----------------------------------------------------------------------------*)
(*------ border stype --------------------------------------------------------*)
TYPE
  THPDF_BSSubtype= Windows.UINT;
CONST
  HPDF_BS_SOLID                      * = 1;
  HPDF_BS_DASHED                     * = 2;
  HPDF_BS_BEVELED                    * = 3;
  HPDF_BS_INSET                      * = 4;
  HPDF_BS_UNDERLINED                 * = 5;

TYPE
  THPDF_PageSizes= Windows.UINT;
CONST
  HPDF_PAGE_SIZE_LETTER              * = 0;
  HPDF_PAGE_SIZE_LEGAL               * = 1;
  HPDF_PAGE_SIZE_A3                  * = 2;
  HPDF_PAGE_SIZE_A4                  * = 3;
  HPDF_PAGE_SIZE_A5                  * = 4;
  HPDF_PAGE_SIZE_B4                  * = 5;
  HPDF_PAGE_SIZE_B5                  * = 6;
  HPDF_PAGE_SIZE_EXECUTIVE           * = 7;
  HPDF_PAGE_SIZE_US4x6               * = 8;
  HPDF_PAGE_SIZE_US4x8               * = 9;
  HPDF_PAGE_SIZE_US5x7               * = 10;
  HPDF_PAGE_SIZE_COMM10              * = 11;
  HPDF_PAGE_SIZE_EOF                 * = 12;

TYPE
  THPDF_PageDirection= Windows.UINT;
CONST
  HPDF_PAGE_PORTRAIT                 * = 0;
  HPDF_PAGE_LANDSCAPE                * = 1;

TYPE
  THPDF_EncoderType= Windows.UINT;
CONST
  HPDF_ENCODER_TYPE_SINGLE_BYTE      * = 1;
  HPDF_ENCODER_TYPE_DOUBLE_BYTE      * = 2;
  HPDF_ENCODER_TYPE_UNINITIALIZED    * = 3;
  HPDF_ENCODER_UNKNOWN               * = 4;

TYPE
  THPDF_ByteType= Windows.UINT;
CONST
  HPDF_BYTE_TYPE_SINGLE              * = 0;
  HPDF_BYTE_TYPE_LEAD                * = 1;
  HPDF_BYTE_TYPE_TRAIL               * = 2;
  HPDF_BYTE_TYPE_UNKNOWN             * = 3;

TYPE
  THPDF_TextAlignment= Windows.UINT;
CONST
  HPDF_TALIGN_LEFT                   * = 0;
  HPDF_TALIGN_RIGHT                  * = 1;
  HPDF_TALIGN_CENTER                 * = 2;
  HPDF_TALIGN_JUSTIFY                * = 3;
TYPE
  THPDF_InfoType * = Windows.UINT;
CONST
    (* date-time type parameters *)
    HPDF_INFO_CREATION_DATE          * = 0;
    HPDF_INFO_MOD_DATE               * = 1;

    (* string type parameters *)
    HPDF_INFO_AUTHOR                 * = 2;
    HPDF_INFO_CREATOR                * = 3;
    HPDF_INFO_PRODUCER               * = 4;
    HPDF_INFO_TITLE                  * = 5;
    HPDF_INFO_SUBJECT                * = 6;
    HPDF_INFO_KEYWORDS               * = 7;
    HPDF_INFO_EOF                    * = 8;

TYPE
  THPDF_EncryptMode = Windows.UINT;
CONST
  HPDF_ENCRYPT_R2 * = 2;
  HPDF_ENCRYPT_R3 * = 3;

TYPE
  THPDF_TransitionStyle * = Windows.UINT;
CONST
  HPDF_TS_WIPE_RIGHT                       * = 0;
  HPDF_TS_WIPE_UP                          * = 1;
  HPDF_TS_WIPE_LEFT                        * = 2;
  HPDF_TS_WIPE_DOWN                        * = 3;
  HPDF_TS_BARN_DOORS_HORIZONTAL_OUT        * = 4;
  HPDF_TS_BARN_DOORS_HORIZONTAL_IN         * = 5;
  HPDF_TS_BARN_DOORS_VERTICAL_OUT          * = 6;
  HPDF_TS_BARN_DOORS_VERTICAL_IN           * = 7;
  HPDF_TS_BOX_OUT                          * = 8;
  HPDF_TS_BOX_IN                           * = 9;
  HPDF_TS_BLINDS_HORIZONTAL                * = 10;
  HPDF_TS_BLINDS_VERTICAL                  * = 11;
  HPDF_TS_DISSOLVE                         * = 12;
  HPDF_TS_GLITTER_RIGHT                    * = 13;
  HPDF_TS_GLITTER_DOWN                     * = 14;
  HPDF_TS_GLITTER_TOP_LEFT_TO_BOTTOM_RIGHT * = 15;
  HPDF_TS_REPLACE                          * = 16;
  HPDF_TS_EOF                              * = 17;

TYPE
  THPDF_BlendMode * = Windows.UINT;
CONST
  HPDF_BM_NORMAL      * = 0;
  HPDF_BM_MULTIPLY    * = 1;
  HPDF_BM_SCREEN      * = 2;
  HPDF_BM_OVERLAY     * = 3;
  HPDF_BM_DARKEN      * = 4;
  HPDF_BM_LIGHTEN     * = 5;
  HPDF_BM_COLOR_DODGE * = 6;
  HPDF_BM_COLOR_BUM   * = 7;
  HPDF_BM_HARD_LIGHT  * = 8;
  HPDF_BM_SOFT_LIGHT  * = 9;
  HPDF_BM_DIFFERENCE  * = 10;
  HPDF_BM_EXCLUSHON   * = 11;
  HPDF_BM_EOF         * = 12;


CONST
  HPDF_TRUE                    * = TRUE;
  HPDF_FALSE                   * = FALSE;

  HPDF_OK                      * = 0;
  HPDF_NOERROR                 * = 0;

(*----- default values -------------------------------------------------------*)

(* buffer size which is required when we convert to character string. *)
  HPDF_TMP_BUF_SIZ             * = 256;
  HPDF_SHORT_BUF_SIZ           * = 32;
  HPDF_REAL_LEN                * = 11;
  HPDF_INT_LEN                 * = 11;
  HPDF_TEXT_DEFAULT_LEN        * = 256;
  HPDF_UNICODE_HEADER_LEN      * = 2;
  HPDF_DATE_TIME_STR_LEN       * = 23;

(* length of each item defined in PDF *)
  HPDF_BYTE_OFFSET_LEN         * = 10;
  HPDF_OBJ_ID_LEN              * = 7;
  HPDF_GEN_NO_LEN              * = 5;

(* default value of Graphic State *)
  HPDF_DEF_FONT                * = 'Helvetica';
  HPDF_DEF_PAGE_LAYOUT         * = HPDF_PAGE_LAYOUT_SINGLE;
  HPDF_DEF_PAGE_MODE           * = HPDF_PAGE_MODE_USE_NONE;
  HPDF_DEF_WORDSPACE           * = 0;
  HPDF_DEF_CHARSPACE           * = 0;
  HPDF_DEF_FONTSIZE            * = 10;
  HPDF_DEF_HSCALING            * = 100;
  HPDF_DEF_LEADING             * = 0;
  HPDF_DEF_RENDERING_MODE      * = HPDF_FILL;
  HPDF_DEF_RAISE               * = 0;
  HPDF_DEF_LINEWIDTH           * = 1;
  HPDF_DEF_LINECAP             * =  HPDF_BUTT_END;
  HPDF_DEF_LINEJOIN            * = HPDF_MITER_JOIN;
  HPDF_DEF_MITERLIMIT          * = 10;
  HPDF_DEF_FLATNESS            * = 1;
  HPDF_DEF_PAGE_NUM            * = 1;

  HPDF_BS_DEF_WIDTH            * = 1;

(* default page-size *)
  HPDF_DEF_PAGE_WIDTH          * = 595.276;
  HPDF_DEF_PAGE_HEIGHT         * = 841.89;

  HPDF_VERSION_TEXT            * = '2.0.3';

(*---------------------------------------------------------------------------*)
(*----- compression mode ----------------------------------------------------*)
TYPE
  TCompressionMode* = SYSTEM.SET32;
CONST
  HPDF_COMP_NONE               * = TCompressionMode { };
  HPDF_COMP_TEXT               * = TCompressionMode {0};
  HPDF_COMP_IMAGE              * = TCompressionMode {1};
  HPDF_COMP_METADATA           * = TCompressionMode {2};
  HPDF_COMP_ALL                * = TCompressionMode {0..3};
(*  HPDF_COMP_BEST_COMPRESS    * = $10;
 *  HPDF_COMP_BEST_SPEED       * = $20;
 *)
  HPDF_COMP_MASK               * = TCompressionMode {0..7};

(*----------------------------------------------------------------------------*)
(*----- permission flags (only Revision 2 is supported)-----------------------*)

  HPDF_ENABLE_READ             * = 0;
  HPDF_ENABLE_PRINT            * = 4;
  HPDF_ENABLE_EDIT_ALL         * = 8;
  HPDF_ENABLE_COPY             * = 16;
  HPDF_ENABLE_EDIT             * = 32;


(*----------------------------------------------------------------------------*)
(*------ viewer preferences definitions --------------------------------------*)

  HPDF_HIDE_TOOLBAR            * = 1;
  HPDF_HIDE_MENUBAR            * = 2;
  HPDF_HIDE_WINDOW_UI          * = 4;
  HPDF_FIT_WINDOW              * = 8;
  HPDF_CENTER_WINDOW           * = 16;


(*---------------------------------------------------------------------------*)
(*------ limitation of object implementation (PDF1.4) -----------------------*)

  HPDF_LIMIT_MAX_INT           * = 2147483647;
  HPDF_LIMIT_MIN_INT           * = -2147483647;

  HPDF_LIMIT_MAX_REAL          * = 32767;
  HPDF_LIMIT_MIN_REAL          * = -32767;

  HPDF_LIMIT_MAX_STRING_LEN    * = 65535;
  HPDF_LIMIT_MAX_NAME_LEN      * = 127;

  HPDF_LIMIT_MAX_ARRAY         * = 8191;
  HPDF_LIMIT_MAX_DICT_ELEMENT  * = 4095;
  HPDF_LIMIT_MAX_XREF_ELEMENT  * = 8388607;
  HPDF_LIMIT_MAX_GSTATE        * = 28;
  HPDF_LIMIT_MAX_DEVICE_N      * = 8;
  HPDF_LIMIT_MAX_DEVICE_N_V15  * = 32;
  HPDF_LIMIT_MAX_CID           * = 65535;
  HPDF_MAX_GENERATION_NUM      * = 65535;

  HPDF_MIN_PAGE_HEIGHT         * = 3;
  HPDF_MIN_PAGE_WIDTH          * = 3;
  HPDF_MAX_PAGE_HEIGHT         * = 14400;
  HPDF_MAX_PAGE_WIDTH          * = 14400;
  HPDF_MIN_MAGNIFICATION_FACTOR* = 8;
  HPDF_MAX_MAGNIFICATION_FACTOR* = 3200;

(*---------------------------------------------------------------------------*)
(*------ limitation of various properties -----------------------------------*)

  HPDF_MIN_PAGE_SIZE           * = 3;
  HPDF_MAX_PAGE_SIZE           * = 14400;
  HPDF_MIN_HORIZONTALSCALING   * = 10;
  HPDF_MAX_HORIZONTALSCALING   * = 300;
  HPDF_MIN_WORDSPACE           * = -30;
  HPDF_MAX_WORDSPACE           * = 300;
  HPDF_MIN_CHARSPACE           * = -30;
  HPDF_MAX_CHARSPACE           * = 300;
  HPDF_MAX_FONTSIZE            * = 300;
  HPDF_MAX_ZOOMSIZE            * = 10;
  HPDF_MAX_LEADING             * = 300;
  HPDF_MAX_LINEWIDTH           * = 100;
  HPDF_MAX_DASH_PATTERN        * = 100;

(*----------------------------------------------------------------------------*)
(*----- country code definition ----------------------------------------------*)

  HPDF_COUNTRY_AF              * = 'AF';    (* AFGHANISTAN *)
  HPDF_COUNTRY_AL              * = 'AL';    (* ALBANIA *)
  HPDF_COUNTRY_DZ              * = 'DZ';    (* ALGERIA *)
  HPDF_COUNTRY_AS              * = 'AS';    (* AMERICAN SAMOA *)
  HPDF_COUNTRY_AD              * = 'AD';    (* ANDORRA *)
  HPDF_COUNTRY_AO              * = 'AO';    (* ANGOLA *)
  HPDF_COUNTRY_AI              * = 'AI';    (* ANGUILLA *)
  HPDF_COUNTRY_AQ              * = 'AQ';    (* ANTARCTICA *)
  HPDF_COUNTRY_AG              * = 'AG';    (* ANTIGUA AND BARBUDA *)
  HPDF_COUNTRY_AR              * = 'AR';    (* ARGENTINA *)
  HPDF_COUNTRY_AM              * = 'AM';    (* ARMENIA *)
  HPDF_COUNTRY_AW              * = 'AW';    (* ARUBA *)
  HPDF_COUNTRY_AU              * = 'AU';    (* AUSTRALIA *)
  HPDF_COUNTRY_AT              * = 'AT';    (* AUSTRIA *)
  HPDF_COUNTRY_AZ              * = 'AZ';    (* AZERBAIJAN *)
  HPDF_COUNTRY_BS              * = 'BS';    (* BAHAMAS *)
  HPDF_COUNTRY_BH              * = 'BH';    (* BAHRAIN *)
  HPDF_COUNTRY_BD              * = 'BD';    (* BANGLADESH *)
  HPDF_COUNTRY_BB              * = 'BB';    (* BARBADOS *)
  HPDF_COUNTRY_BY              * = 'BY';    (* BELARUS *)
  HPDF_COUNTRY_BE              * = 'BE';    (* BELGIUM *)
  HPDF_COUNTRY_BZ              * = 'BZ';    (* BELIZE *)
  HPDF_COUNTRY_BJ              * = 'BJ';    (* BENIN *)
  HPDF_COUNTRY_BM              * = 'BM';    (* BERMUDA *)
  HPDF_COUNTRY_BT              * = 'BT';    (* BHUTAN *)
  HPDF_COUNTRY_BO              * = 'BO';    (* BOLIVIA *)
  HPDF_COUNTRY_BA              * = 'BA';    (* BOSNIA AND HERZEGOWINA *)
  HPDF_COUNTRY_BW              * = 'BW';    (* BOTSWANA *)
  HPDF_COUNTRY_BV              * = 'BV';    (* BOUVET ISLAND *)
  HPDF_COUNTRY_BR              * = 'BR';    (* BRAZIL *)
  HPDF_COUNTRY_IO              * = 'IO';    (* BRITISH INDIAN OCEAN TERRITORY *)
  HPDF_COUNTRY_BN              * = 'BN';    (* BRUNEI DARUSSALAM *)
  HPDF_COUNTRY_BG              * = 'BG';    (* BULGARIA *)
  HPDF_COUNTRY_BF              * = 'BF';    (* BURKINA FASO *)
  HPDF_COUNTRY_BI              * = 'BI';    (* BURUNDI *)
  HPDF_COUNTRY_KH              * = 'KH';    (* CAMBODIA *)
  HPDF_COUNTRY_CM              * = 'CM';    (* CAMEROON *)
  HPDF_COUNTRY_CA              * = 'CA';    (* CANADA *)
  HPDF_COUNTRY_CV              * = 'CV';    (* CAPE VERDE *)
  HPDF_COUNTRY_KY              * = 'KY';    (* CAYMAN ISLANDS *)
  HPDF_COUNTRY_CF              * = 'CF';    (* CENTRAL AFRICAN REPUBLIC *)
  HPDF_COUNTRY_TD              * = 'TD';    (* CHAD *)
  HPDF_COUNTRY_CL              * = 'CL';    (* CHILE *)
  HPDF_COUNTRY_CN              * = 'CN';    (* CHINA *)
  HPDF_COUNTRY_CX              * = 'CX';    (* CHRISTMAS ISLAND *)
  HPDF_COUNTRY_CC              * = 'CC';    (* COCOS (KEELING) ISLANDS *)
  HPDF_COUNTRY_CO              * = 'CO';    (* COLOMBIA *)
  HPDF_COUNTRY_KM              * = 'KM';    (* COMOROS *)
  HPDF_COUNTRY_CG              * = 'CG';    (* CONGO *)
  HPDF_COUNTRY_CK              * = 'CK';    (* COOK ISLANDS *)
  HPDF_COUNTRY_CR              * = 'CR';    (* COSTA RICA *)
  HPDF_COUNTRY_CI              * = 'CI';    (* COTE D'IVOIRE *)
  HPDF_COUNTRY_HR              * = 'HR';    (* CROATIA (local name: Hrvatska) *)
  HPDF_COUNTRY_CU              * = 'CU';    (* CUBA *)
  HPDF_COUNTRY_CY              * = 'CY';    (* CYPRUS *)
  HPDF_COUNTRY_CZ              * = 'CZ';    (* CZECH REPUBLIC *)
  HPDF_COUNTRY_DK              * = 'DK';    (* DENMARK *)
  HPDF_COUNTRY_DJ              * = 'DJ';    (* DJIBOUTI *)
  HPDF_COUNTRY_DM              * = 'DM';    (* DOMINICA *)
  HPDF_COUNTRY_DO              * = 'DO';    (* DOMINICAN REPUBLIC *)
  HPDF_COUNTRY_TP              * = 'TP';    (* EAST TIMOR *)
  HPDF_COUNTRY_EC              * = 'EC';    (* ECUADOR *)
  HPDF_COUNTRY_EG              * = 'EG';    (* EGYPT *)
  HPDF_COUNTRY_SV              * = 'SV';    (* EL SALVADOR *)
  HPDF_COUNTRY_GQ              * = 'GQ';    (* EQUATORIAL GUINEA *)
  HPDF_COUNTRY_ER              * = 'ER';    (* ERITREA *)
  HPDF_COUNTRY_EE              * = 'EE';    (* ESTONIA *)
  HPDF_COUNTRY_ET              * = 'ET';    (* ETHIOPIA *)
  HPDF_COUNTRY_FK              * = 'FK';    (* FALKLAND ISLANDS (MALVINAS) *)
  HPDF_COUNTRY_FO              * = 'FO';    (* FAROE ISLANDS *)
  HPDF_COUNTRY_FJ              * = 'FJ';    (* FIJI *)
  HPDF_COUNTRY_FI              * = 'FI';    (* FINLAND *)
  HPDF_COUNTRY_FR              * = 'FR';    (* FRANCE *)
  HPDF_COUNTRY_FX              * = 'FX';    (* FRANCE, METROPOLITAN *)
  HPDF_COUNTRY_GF              * = 'GF';    (* FRENCH GUIANA *)
  HPDF_COUNTRY_PF              * = 'PF';    (* FRENCH POLYNESIA *)
  HPDF_COUNTRY_TF              * = 'TF';    (* FRENCH SOUTHERN TERRITORIES *)
  HPDF_COUNTRY_GA              * = 'GA';    (* GABON *)
  HPDF_COUNTRY_GM              * = 'GM';    (* GAMBIA *)
  HPDF_COUNTRY_GE              * = 'GE';    (* GEORGIA *)
  HPDF_COUNTRY_DE              * = 'DE';    (* GERMANY *)
  HPDF_COUNTRY_GH              * = 'GH';    (* GHANA *)
  HPDF_COUNTRY_GI              * = 'GI';    (* GIBRALTAR *)
  HPDF_COUNTRY_GR              * = 'GR';    (* GREECE *)
  HPDF_COUNTRY_GL              * = 'GL';    (* GREENLAND *)
  HPDF_COUNTRY_GD              * = 'GD';    (* GRENADA *)
  HPDF_COUNTRY_GP              * = 'GP';    (* GUADELOUPE *)
  HPDF_COUNTRY_GU              * = 'GU';    (* GUAM *)
  HPDF_COUNTRY_GT              * = 'GT';    (* GUATEMALA *)
  HPDF_COUNTRY_GN              * = 'GN';    (* GUINEA *)
  HPDF_COUNTRY_GW              * = 'GW';    (* GUINEA-BISSAU *)
  HPDF_COUNTRY_GY              * = 'GY';    (* GUYANA *)
  HPDF_COUNTRY_HT              * = 'HT';    (* HAITI *)
  HPDF_COUNTRY_HM              * = 'HM';    (* HEARD AND MC DONALD ISLANDS *)
  HPDF_COUNTRY_HN              * = 'HN';    (* HONDURAS *)
  HPDF_COUNTRY_HK              * = 'HK';    (* HONG KONG *)
  HPDF_COUNTRY_HU              * = 'HU';    (* HUNGARY *)
  HPDF_COUNTRY_IS              * = 'IS';    (* ICELAND *)
  HPDF_COUNTRY_IN              * = 'IN';    (* INDIA *)
  HPDF_COUNTRY_ID              * = 'ID';    (* INDONESIA *)
  HPDF_COUNTRY_IR              * = 'IR';    (* IRAN (ISLAMIC REPUBLIC OF) *)
  HPDF_COUNTRY_IQ              * = 'IQ';    (* IRAQ *)
  HPDF_COUNTRY_IE              * = 'IE';    (* IRELAND *)
  HPDF_COUNTRY_IL              * = 'IL';    (* ISRAEL *)
  HPDF_COUNTRY_IT              * = 'IT';    (* ITALY *)
  HPDF_COUNTRY_JM              * = 'JM';    (* JAMAICA *)
  HPDF_COUNTRY_JP              * = 'JP';    (* JAPAN *)
  HPDF_COUNTRY_JO              * = 'JO';    (* JORDAN *)
  HPDF_COUNTRY_KZ              * = 'KZ';    (* KAZAKHSTAN *)
  HPDF_COUNTRY_KE              * = 'KE';    (* KENYA *)
  HPDF_COUNTRY_KI              * = 'KI';    (* KIRIBATI *)
  HPDF_COUNTRY_KP              * = 'KP';    (* KOREA, DEMOCRATIC PEOPLE'S REPUBLIC OF *)
  HPDF_COUNTRY_KR              * = 'KR';    (* KOREA, REPUBLIC OF *)
  HPDF_COUNTRY_KW              * = 'KW';    (* KUWAIT *)
  HPDF_COUNTRY_KG              * = 'KG';    (* KYRGYZSTAN *)
  HPDF_COUNTRY_LA              * = 'LA';    (* LAO PEOPLE'S DEMOCRATIC REPUBLIC *)
  HPDF_COUNTRY_LV              * = 'LV';    (* LATVIA *)
  HPDF_COUNTRY_LB              * = 'LB';    (* LEBANON *)
  HPDF_COUNTRY_LS              * = 'LS';    (* LESOTHO *)
  HPDF_COUNTRY_LR              * = 'LR';    (* LIBERIA *)
  HPDF_COUNTRY_LY              * = 'LY';    (* LIBYAN ARAB JAMAHIRIYA *)
  HPDF_COUNTRY_LI              * = 'LI';    (* LIECHTENSTEIN *)
  HPDF_COUNTRY_LT              * = 'LT';    (* LITHUANIA *)
  HPDF_COUNTRY_LU              * = 'LU';    (* LUXEMBOURG *)
  HPDF_COUNTRY_MO              * = 'MO';    (* MACAU *)
  HPDF_COUNTRY_MK              * = 'MK';    (* MACEDONIA,THE FORMER YUGOSLAV REPUBLIC OF *)
  HPDF_COUNTRY_MG              * = 'MG';    (* MADAGASCAR *)
  HPDF_COUNTRY_MW              * = 'MW';    (* MALAWI *)
  HPDF_COUNTRY_MY              * = 'MY';    (* MALAYSIA *)
  HPDF_COUNTRY_MV              * = 'MV';    (* MALDIVES *)
  HPDF_COUNTRY_ML              * = 'ML';    (* MALI *)
  HPDF_COUNTRY_MT              * = 'MT';    (* MALTA *)
  HPDF_COUNTRY_MH              * = 'MH';    (* MARSHALL ISLANDS *)
  HPDF_COUNTRY_MQ              * = 'MQ';    (* MARTINIQUE *)
  HPDF_COUNTRY_MR              * = 'MR';    (* MAURITANIA *)
  HPDF_COUNTRY_MU              * = 'MU';    (* MAURITIUS *)
  HPDF_COUNTRY_YT              * = 'YT';    (* MAYOTTE *)
  HPDF_COUNTRY_MX              * = 'MX';    (* MEXICO *)
  HPDF_COUNTRY_FM              * = 'FM';    (* MICRONESIA, FEDERATED STATES OF *)
  HPDF_COUNTRY_MD              * = 'MD';    (* MOLDOVA, REPUBLIC OF *)
  HPDF_COUNTRY_MC              * = 'MC';    (* MONACO *)
  HPDF_COUNTRY_MN              * = 'MN';    (* MONGOLIA *)
  HPDF_COUNTRY_MS              * = 'MS';    (* MONTSERRAT *)
  HPDF_COUNTRY_MA              * = 'MA';    (* MOROCCO *)
  HPDF_COUNTRY_MZ              * = 'MZ';    (* MOZAMBIQUE *)
  HPDF_COUNTRY_MM              * = 'MM';    (* MYANMAR *)
  HPDF_COUNTRY_NA              * = 'NA';    (* NAMIBIA *)
  HPDF_COUNTRY_NR              * = 'NR';    (* NAURU *)
  HPDF_COUNTRY_NP              * = 'NP';    (* NEPAL *)
  HPDF_COUNTRY_NL              * = 'NL';    (* NETHERLANDS *)
  HPDF_COUNTRY_AN              * = 'AN';    (* NETHERLANDS ANTILLES *)
  HPDF_COUNTRY_NC              * = 'NC';    (* NEW CALEDONIA *)
  HPDF_COUNTRY_NZ              * = 'NZ';    (* NEW ZEALAND *)
  HPDF_COUNTRY_NI              * = 'NI';    (* NICARAGUA *)
  HPDF_COUNTRY_NE              * = 'NE';    (* NIGER *)
  HPDF_COUNTRY_NG              * = 'NG';    (* NIGERIA *)
  HPDF_COUNTRY_NU              * = 'NU';    (* NIUE *)
  HPDF_COUNTRY_NF              * = 'NF';    (* NORFOLK ISLAND *)
  HPDF_COUNTRY_MP              * = 'MP';    (* NORTHERN MARIANA ISLANDS *)
  HPDF_COUNTRY_NO              * = 'NO';    (* NORWAY *)
  HPDF_COUNTRY_OM              * = 'OM';    (* OMAN *)
  HPDF_COUNTRY_PK              * = 'PK';    (* PAKISTAN *)
  HPDF_COUNTRY_PW              * = 'PW';    (* PALAU *)
  HPDF_COUNTRY_PA              * = 'PA';    (* PANAMA *)
  HPDF_COUNTRY_PG              * = 'PG';    (* PAPUA NEW GUINEA *)
  HPDF_COUNTRY_PY              * = 'PY';    (* PARAGUAY *)
  HPDF_COUNTRY_PE              * = 'PE';    (* PERU *)
  HPDF_COUNTRY_PH              * = 'PH';    (* PHILIPPINES *)
  HPDF_COUNTRY_PN              * = 'PN';    (* PITCAIRN *)
  HPDF_COUNTRY_PL              * = 'PL';    (* POLAND *)
  HPDF_COUNTRY_PT              * = 'PT';    (* PORTUGAL *)
  HPDF_COUNTRY_PR              * = 'PR';    (* PUERTO RICO *)
  HPDF_COUNTRY_QA              * = 'QA';    (* QATAR *)
  HPDF_COUNTRY_RE              * = 'RE';    (* REUNION *)
  HPDF_COUNTRY_RO              * = 'RO';    (* ROMANIA *)
  HPDF_COUNTRY_RU              * = 'RU';    (* RUSSIAN FEDERATION *)
  HPDF_COUNTRY_RW              * = 'RW';    (* RWANDA *)
  HPDF_COUNTRY_KN              * = 'KN';    (* SAINT KITTS AND NEVIS *)
  HPDF_COUNTRY_LC              * = 'LC';    (* SAINT LUCIA *)
  HPDF_COUNTRY_VC              * = 'VC';    (* SAINT VINCENT AND THE GRENADINES *)
  HPDF_COUNTRY_WS              * = 'WS';    (* SAMOA *)
  HPDF_COUNTRY_SM              * = 'SM';    (* SAN MARINO *)
  HPDF_COUNTRY_ST              * = 'ST';    (* SAO TOME AND PRINCIPE *)
  HPDF_COUNTRY_SA              * = 'SA';    (* SAUDI ARABIA *)
  HPDF_COUNTRY_SN              * = 'SN';    (* SENEGAL *)
  HPDF_COUNTRY_SC              * = 'SC';    (* SEYCHELLES *)
  HPDF_COUNTRY_SL              * = 'SL';    (* SIERRA LEONE *)
  HPDF_COUNTRY_SG              * = 'SG';    (* SINGAPORE *)
  HPDF_COUNTRY_SK              * = 'SK';    (* SLOVAKIA (Slovak Republic) *)
  HPDF_COUNTRY_SI              * = 'SI';    (* SLOVENIA *)
  HPDF_COUNTRY_SB              * = 'SB';    (* SOLOMON ISLANDS *)
  HPDF_COUNTRY_SO              * = 'SO';    (* SOMALIA *)
  HPDF_COUNTRY_ZA              * = 'ZA';    (* SOUTH AFRICA *)
  HPDF_COUNTRY_ES              * = 'ES';    (* SPAIN *)
  HPDF_COUNTRY_LK              * = 'LK';    (* SRI LANKA *)
  HPDF_COUNTRY_SH              * = 'SH';    (* ST. HELENA *)
  HPDF_COUNTRY_PM              * = 'PM';    (* ST. PIERRE AND MIQUELON *)
  HPDF_COUNTRY_SD              * = 'SD';    (* SUDAN *)
  HPDF_COUNTRY_SR              * = 'SR';    (* SURINAME *)
  HPDF_COUNTRY_SJ              * = 'SJ';    (* SVALBARD AND JAN MAYEN ISLANDS *)
  HPDF_COUNTRY_SZ              * = 'SZ';    (* SWAZILAND *)
  HPDF_COUNTRY_SE              * = 'SE';    (* SWEDEN *)
  HPDF_COUNTRY_CH              * = 'CH';    (* SWITZERLAND *)
  HPDF_COUNTRY_SY              * = 'SY';    (* SYRIAN ARAB REPUBLIC *)
  HPDF_COUNTRY_TW              * = 'TW';    (* TAIWAN, PROVINCE OF CHINA *)
  HPDF_COUNTRY_TJ              * = 'TJ';    (* TAJIKISTAN *)
  HPDF_COUNTRY_TZ              * = 'TZ';    (* TANZANIA, UNITED REPUBLIC OF *)
  HPDF_COUNTRY_TH              * = 'TH';    (* THAILAND *)
  HPDF_COUNTRY_TG              * = 'TG';    (* TOGO *)
  HPDF_COUNTRY_TK              * = 'TK';    (* TOKELAU *)
  HPDF_COUNTRY_TO              * = 'TO';    (* TONGA *)
  HPDF_COUNTRY_TT              * = 'TT';    (* TRINIDAD AND TOBAGO *)
  HPDF_COUNTRY_TN              * = 'TN';    (* TUNISIA *)
  HPDF_COUNTRY_TR              * = 'TR';    (* TURKEY *)
  HPDF_COUNTRY_TM              * = 'TM';    (* TURKMENISTAN *)
  HPDF_COUNTRY_TC              * = 'TC';    (* TURKS AND CAICOS ISLANDS *)
  HPDF_COUNTRY_TV              * = 'TV';    (* TUVALU *)
  HPDF_COUNTRY_UG              * = 'UG';    (* UGANDA *)
  HPDF_COUNTRY_UA              * = 'UA';    (* UKRAINE *)
  HPDF_COUNTRY_AE              * = 'AE';    (* UNITED ARAB EMIRATES *)
  HPDF_COUNTRY_GB              * = 'GB';    (* UNITED KINGDOM *)
  HPDF_COUNTRY_US              * = 'US';    (* UNITED STATES *)
  HPDF_COUNTRY_UM              * = 'UM';    (* UNITED STATES MINOR OUTLYING ISLANDS *)
  HPDF_COUNTRY_UY              * = 'UY';    (* URUGUAY *)
  HPDF_COUNTRY_UZ              * = 'UZ';    (* UZBEKISTAN *)
  HPDF_COUNTRY_VU              * = 'VU';    (* VANUATU *)
  HPDF_COUNTRY_VA              * = 'VA';    (* VATICAN CITY STATE (HOLY SEE) *)
  HPDF_COUNTRY_VE              * = 'VE';    (* VENEZUELA *)
  HPDF_COUNTRY_VN              * = 'VN';    (* VIET NAM *)
  HPDF_COUNTRY_VG              * = 'VG';    (* VIRGIN ISLANDS (BRITISH) *)
  HPDF_COUNTRY_VI              * = 'VI';    (* VIRGIN ISLANDS (U.S.) *)
  HPDF_COUNTRY_WF              * = 'WF';    (* WALLIS AND FUTUNA ISLANDS *)
  HPDF_COUNTRY_EH              * = 'EH';    (* WESTERN SAHARA *)
  HPDF_COUNTRY_YE              * = 'YE';    (* YEMEN *)
  HPDF_COUNTRY_YU              * = 'YU';    (* YUGOSLAVIA *)
  HPDF_COUNTRY_ZR              * = 'ZR';    (* ZAIRE *)
  HPDF_COUNTRY_ZM              * = 'ZM';    (* ZAMBIA *)
  HPDF_COUNTRY_ZW              * = 'ZW';    (* ZIMBABWE *)

(*----------------------------------------------------------------------------*)
(*----- lang code definition -------------------------------------------------*)

  HPDF_LANG_AA                 * = 'aa';    (* Afar *)
  HPDF_LANG_AB                 * = 'ab';    (* Abkhazian *)
  HPDF_LANG_AF                 * = 'af';    (* Afrikaans *)
  HPDF_LANG_AM                 * = 'am';    (* Amharic *)
  HPDF_LANG_AR                 * = 'ar';    (* Arabic *)
  HPDF_LANG_AS                 * = 'as';    (* Assamese *)
  HPDF_LANG_AY                 * = 'ay';    (* Aymara *)
  HPDF_LANG_AZ                 * = 'az';    (* Azerbaijani *)
  HPDF_LANG_BA                 * = 'ba';    (* Bashkir *)
  HPDF_LANG_BE                 * = 'be';    (* Byelorussian *)
  HPDF_LANG_BG                 * = 'bg';    (* Bulgarian *)
  HPDF_LANG_BH                 * = 'bh';    (* Bihari *)
  HPDF_LANG_BI                 * = 'bi';    (* Bislama *)
  HPDF_LANG_BN                 * = 'bn';    (* Bengali Bangla *)
  HPDF_LANG_BO                 * = 'bo';    (* Tibetan *)
  HPDF_LANG_BR                 * = 'br';    (* Breton *)
  HPDF_LANG_CA                 * = 'ca';    (* Catalan *)
  HPDF_LANG_CO                 * = 'co';    (* Corsican *)
  HPDF_LANG_CS                 * = 'cs';    (* Czech *)
  HPDF_LANG_CY                 * = 'cy';    (* Welsh *)
  HPDF_LANG_DA                 * = 'da';    (* Danish *)
  HPDF_LANG_DE                 * = 'de';    (* German *)
  HPDF_LANG_DZ                 * = 'dz';    (* Bhutani *)
  HPDF_LANG_EL                 * = 'el';    (* Greek *)
  HPDF_LANG_EN                 * = 'en';    (* English *)
  HPDF_LANG_EO                 * = 'eo';    (* Esperanto *)
  HPDF_LANG_ES                 * = 'es';    (* Spanish *)
  HPDF_LANG_ET                 * = 'et';    (* Estonian *)
  HPDF_LANG_EU                 * = 'eu';    (* Basque *)
  HPDF_LANG_FA                 * = 'fa';    (* Persian *)
  HPDF_LANG_FI                 * = 'fi';    (* Finnish *)
  HPDF_LANG_FJ                 * = 'fj';    (* Fiji *)
  HPDF_LANG_FO                 * = 'fo';    (* Faeroese *)
  HPDF_LANG_FR                 * = 'fr';    (* French *)
  HPDF_LANG_FY                 * = 'fy';    (* Frisian *)
  HPDF_LANG_GA                 * = 'ga';    (* Irish *)
  HPDF_LANG_GD                 * = 'gd';    (* Scots Gaelic *)
  HPDF_LANG_GL                 * = 'gl';    (* Galician *)
  HPDF_LANG_GN                 * = 'gn';    (* Guarani *)
  HPDF_LANG_GU                 * = 'gu';    (* Gujarati *)
  HPDF_LANG_HA                 * = 'ha';    (* Hausa *)
  HPDF_LANG_HI                 * = 'hi';    (* Hindi *)
  HPDF_LANG_HR                 * = 'hr';    (* Croatian *)
  HPDF_LANG_HU                 * = 'hu';    (* Hungarian *)
  HPDF_LANG_HY                 * = 'hy';    (* Armenian *)
  HPDF_LANG_IA                 * = 'ia';    (* Interlingua *)
  HPDF_LANG_IE                 * = 'ie';    (* Interlingue *)
  HPDF_LANG_IK                 * = 'ik';    (* Inupiak *)
  HPDF_LANG_IN                 * = 'in';    (* Indonesian *)
  HPDF_LANG_IS                 * = 'is';    (* Icelandic *)
  HPDF_LANG_IT                 * = 'it';    (* Italian *)
  HPDF_LANG_IW                 * = 'iw';    (* Hebrew *)
  HPDF_LANG_JA                 * = 'ja';    (* Japanese *)
  HPDF_LANG_JI                 * = 'ji';    (* Yiddish *)
  HPDF_LANG_JW                 * = 'jw';    (* Javanese *)
  HPDF_LANG_KA                 * = 'ka';    (* Georgian *)
  HPDF_LANG_KK                 * = 'kk';    (* Kazakh *)
  HPDF_LANG_KL                 * = 'kl';    (* Greenlandic *)
  HPDF_LANG_KM                 * = 'km';    (* Cambodian *)
  HPDF_LANG_KN                 * = 'kn';    (* Kannada *)
  HPDF_LANG_KO                 * = 'ko';    (* Korean *)
  HPDF_LANG_KS                 * = 'ks';    (* Kashmiri *)
  HPDF_LANG_KU                 * = 'ku';    (* Kurdish *)
  HPDF_LANG_KY                 * = 'ky';    (* Kirghiz *)
  HPDF_LANG_LA                 * = 'la';    (* Latin *)
  HPDF_LANG_LN                 * = 'ln';    (* Lingala *)
  HPDF_LANG_LO                 * = 'lo';    (* Laothian *)
  HPDF_LANG_LT                 * = 'lt';    (* Lithuanian *)
  HPDF_LANG_LV                 * = 'lv';    (* Latvian,Lettish *)
  HPDF_LANG_MG                 * = 'mg';    (* Malagasy *)
  HPDF_LANG_MI                 * = 'mi';    (* Maori *)
  HPDF_LANG_MK                 * = 'mk';    (* Macedonian *)
  HPDF_LANG_ML                 * = 'ml';    (* Malayalam *)
  HPDF_LANG_MN                 * = 'mn';    (* Mongolian *)
  HPDF_LANG_MO                 * = 'mo';    (* Moldavian *)
  HPDF_LANG_MR                 * = 'mr';    (* Marathi *)
  HPDF_LANG_MS                 * = 'ms';    (* Malay *)
  HPDF_LANG_MT                 * = 'mt';    (* Maltese *)
  HPDF_LANG_MY                 * = 'my';    (* Burmese *)
  HPDF_LANG_NA                 * = 'na';    (* Nauru *)
  HPDF_LANG_NE                 * = 'ne';    (* Nepali *)
  HPDF_LANG_NL                 * = 'nl';    (* Dutch *)
  HPDF_LANG_NO                 * = 'no';    (* Norwegian *)
  HPDF_LANG_OC                 * = 'oc';    (* Occitan *)
  HPDF_LANG_OM                 * = 'om';    (* (Afan)Oromo *)
  HPDF_LANG_OR                 * = 'or';    (* Oriya *)
  HPDF_LANG_PA                 * = 'pa';    (* Punjabi *)
  HPDF_LANG_PL                 * = 'pl';    (* Polish *)
  HPDF_LANG_PS                 * = 'ps';    (* Pashto,Pushto *)
  HPDF_LANG_PT                 * = 'pt';    (* Portuguese  *)
  HPDF_LANG_QU                 * = 'qu';    (* Quechua *)
  HPDF_LANG_RM                 * = 'rm';    (* Rhaeto-Romance *)
  HPDF_LANG_RN                 * = 'rn';    (* Kirundi *)
  HPDF_LANG_RO                 * = 'ro';    (* Romanian *)
  HPDF_LANG_RU                 * = 'ru';    (* Russian *)
  HPDF_LANG_RW                 * = 'rw';    (* Kinyarwanda *)
  HPDF_LANG_SA                 * = 'sa';    (* Sanskrit *)
  HPDF_LANG_SD                 * = 'sd';    (* Sindhi *)
  HPDF_LANG_SG                 * = 'sg';    (* Sangro *)
  HPDF_LANG_SH                 * = 'sh';    (* Serbo-Croatian *)
  HPDF_LANG_SI                 * = 'si';    (* Singhalese *)
  HPDF_LANG_SK                 * = 'sk';    (* Slovak *)
  HPDF_LANG_SL                 * = 'sl';    (* Slovenian *)
  HPDF_LANG_SM                 * = 'sm';    (* Samoan *)
  HPDF_LANG_SN                 * = 'sn';    (* Shona *)
  HPDF_LANG_SO                 * = 'so';    (* Somali *)
  HPDF_LANG_SQ                 * = 'sq';    (* Albanian *)
  HPDF_LANG_SR                 * = 'sr';    (* Serbian *)
  HPDF_LANG_SS                 * = 'ss';    (* Siswati *)
  HPDF_LANG_ST                 * = 'st';    (* Sesotho *)
  HPDF_LANG_SU                 * = 'su';    (* Sundanese *)
  HPDF_LANG_SV                 * = 'sv';    (* Swedish *)
  HPDF_LANG_SW                 * = 'sw';    (* Swahili *)
  HPDF_LANG_TA                 * = 'ta';    (* Tamil *)
  HPDF_LANG_TE                 * = 'te';    (* Tegulu *)
  HPDF_LANG_TG                 * = 'tg';    (* Tajik *)
  HPDF_LANG_TH                 * = 'th';    (* Thai *)
  HPDF_LANG_TI                 * = 'ti';    (* Tigrinya *)
  HPDF_LANG_TK                 * = 'tk';    (* Turkmen *)
  HPDF_LANG_TL                 * = 'tl';    (* Tagalog *)
  HPDF_LANG_TN                 * = 'tn';    (* Setswanato Tonga *)
  HPDF_LANG_TR                 * = 'tr';    (* Turkish *)
  HPDF_LANG_TS                 * = 'ts';    (* Tsonga *)
  HPDF_LANG_TT                 * = 'tt';    (* Tatar *)
  HPDF_LANG_TW                 * = 'tw';    (* Twi *)
  HPDF_LANG_UK                 * = 'uk';    (* Ukrainian *)
  HPDF_LANG_UR                 * = 'ur';    (* Urdu *)
  HPDF_LANG_UZ                 * = 'uz';    (* Uzbek *)
  HPDF_LANG_VI                 * = 'vi';    (* Vietnamese *)
  HPDF_LANG_VO                 * = 'vo';    (* Volapuk *)
  HPDF_LANG_WO                 * = 'wo';    (* Wolof *)
  HPDF_LANG_XH                 * = 'xh';    (* Xhosa *)
  HPDF_LANG_YO                 * = 'yo';    (* Yoruba *)
  HPDF_LANG_ZH                 * = 'zh';    (* Chinese *)
  HPDF_LANG_ZU                 * = 'zu';    (* Zulu *)


(*----------------------------------------------------------------------------*)
(*----- Graphis mode ---------------------------------------------------------*)
CONST
  HPDF_GMODE_PAGE_DESCRIPTION  * = {0};
  HPDF_GMODE_PATH_OBJECT       * = {1};
  HPDF_GMODE_TEXT_OBJECT       * = {2};
  HPDF_GMODE_CLIPPING_PATH     * = {3};
  HPDF_GMODE_SHADING           * = {4};
  HPDF_GMODE_INLINE_IMAGE      * = {5};
  HPDF_GMODE_EXTERNAL_OBJECT   * = {6};


TYPE
  THPDF_ErrorFunc * = PROCEDURE ["StdCall"] (error_no: HPDF_STATUS; detail_no: HPDF_STATUS; user_data: Windows.PVOID); 
  THPDF_AllocFunc * = PROCEDURE ["StdCall"] (size: Windows.UINT); 
  THPDF_FreeFunc  * = PROCEDURE ["StdCall"] (aptr: Windows.PVOID);

  (*
  THPDF_ErrorFunc * = PROCEDURE ["StdCall"] (HPDF_STATUS, HPDF_STATUS, Windows.PVOID); 
  THPDF_AllocFunc * = PROCEDURE ["StdCall"] (Windows.UINT); 
  THPDF_FreeFunc  * = PROCEDURE ["StdCall"] (Windows.PVOID); *)

PROCEDURE ["StdCall"] / HPDF_NewEx*(user_error_fn: THPDF_ErrorFunc; user_alloc_fn: THPDF_AllocFunc; user_free_fn: THPDF_FreeFunc; mem_pool_buf_size: HPDF_UINT; user_data: Windows.PVOID): HPDF_Doc;
PROCEDURE ["StdCall"] / HPDF_New*(user_error_fn: THPDF_ErrorFunc; user_data: Windows.PVOID): HPDF_Doc;
PROCEDURE ["StdCall"] / HPDF_SetErrorHandler*(pdf: HPDF_Doc; user_error_fn: THPDF_ErrorFunc): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Free*(pdf: HPDF_Doc); 
PROCEDURE ["StdCall"] / HPDF_NewDoc*(pdf: HPDF_Doc): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_FreeDoc*(pdf: HPDF_Doc); 
PROCEDURE ["StdCall"] / HPDF_HasDoc*(pdf: HPDF_Doc): HPDF_BOOL; 
PROCEDURE ["StdCall"] / HPDF_FreeDocAll*(pdf: HPDF_Doc); 
PROCEDURE ["StdCall"] / HPDF_SaveToStream*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_GetStreamSize*(pdf: HPDF_Doc): HPDF_UINT32;
PROCEDURE ["StdCall"] / HPDF_ReadFromStream*(pdf: HPDF_Doc; buf: HPDF_PBYTE; size: HPDF_PUINT): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_ResetStream*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_SaveToFile*(pdf: HPDF_Doc; file_name: HPDF_PCHAR): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_GetError*(pdf: HPDF_Doc): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_ResetError*(pdf: HPDF_Doc); 
PROCEDURE ["StdCall"] / HPDF_SetPagesConfiguration*(pdf: HPDF_Doc; page_per_pages: HPDF_UINT): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_GetPageByIndex*(pdf: HPDF_Doc; index: HPDF_UINT): HPDF_Page;
PROCEDURE ["StdCall"] / HPDF_GetPageLayout*(pdf: HPDF_Doc): THPDF_PageLayout;
PROCEDURE ["StdCall"] / HPDF_SetPageLayout*(pdf: HPDF_Doc; layout: THPDF_PageLayout): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_GetPageMode*(pdf: HPDF_Doc): THPDF_PageMode;
PROCEDURE ["StdCall"] / HPDF_SetPageMode*(pdf: HPDF_Doc; mode: THPDF_PageMode): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_SetOpenAction*(pdf: HPDF_Doc; open_action: HPDF_Destination): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_GetViewerPreference*(pdf: HPDF_Doc): HPDF_UINT;
PROCEDURE ["StdCall"] / HPDF_SetViewerPreference*(pdf: HPDF_Doc; value: HPDF_UINT): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_GetCurrentPage*(pdf: HPDF_Doc): HPDF_Page;
PROCEDURE ["StdCall"] / HPDF_AddPage*(pdf: HPDF_Doc): HPDF_Page; 
PROCEDURE ["StdCall"] / HPDF_InsertPage*(pdf: HPDF_Doc; page: HPDF_Page): HPDF_Page;
PROCEDURE ["StdCall"] / HPDF_Page_SetWidth*(page: HPDF_Page; value: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetHeight*(page: HPDF_Page; value: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetSize*(page: HPDF_Page; size: THPDF_PageSizes; direction: THPDF_PageDirection): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetRotate*(page: HPDF_Page; angle: HPDF_UINT16): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_GetFont*(pdf: HPDF_Doc; font_name: HPDF_PCHAR; encoding_name: HPDF_PCHAR): HPDF_Font; 
PROCEDURE ["StdCall"] / HPDF_LoadType1FontFromFile*(pdf: HPDF_Doc; afmfilename: HPDF_PCHAR; pfmfilename: HPDF_PCHAR): HPDF_PCHAR; 
PROCEDURE ["StdCall"] / HPDF_LoadTTFontFromFile*(pdf: HPDF_Doc; file_name: HPDF_PCHAR; embedding: HPDF_BOOL): HPDF_PCHAR; 
PROCEDURE ["StdCall"] / HPDF_LoadTTFontFromFile2*(pdf: HPDF_Doc; file_name: HPDF_PCHAR; index: HPDF_UINT; embedding: HPDF_BOOL): HPDF_CHAR;
PROCEDURE ["StdCall"] / HPDF_AddPageLabel*(pdf: HPDF_Doc; page_num: HPDF_UINT; style: THPDF_PageNumStyle; first_page: HPDF_UINT; prefix: HPDF_PCHAR): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_UseJPFonts*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_UseKRFonts*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_UseCNSFonts*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_UseCNTFonts*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_CreateOutline*(pdf: HPDF_Doc; parent: HPDF_Outline; title: HPDF_PCHAR; encoder: HPDF_Encoder): HPDF_Outline;
PROCEDURE ["StdCall"] / HPDF_Outline_SetOpened*(outline: HPDF_Outline; opened: HPDF_BOOL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Outline_SetDestination*(outline: HPDF_Outline; dst: HPDF_Destination): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_CreateDestination*(page: HPDF_Page): HPDF_Destination;
PROCEDURE ["StdCall"] / HPDF_Destination_SetXYZ*(dst: HPDF_Destination; left: HPDF_REAL; top: HPDF_REAL; zoom: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Destination_SetFit*(dst: HPDF_Destination): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Destination_SetFitH*(dst: HPDF_Destination; top: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Destination_SetFitV*(dst: HPDF_Destination; left: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Destination_SetFitR*(dst: HPDF_Destination; left: HPDF_REAL; bottom: HPDF_REAL; right: HPDF_REAL; top: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Destination_SetFitB*(dst: HPDF_Destination): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Destination_SetFitBH*(dst: HPDF_Destination; top: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Destination_SetFitBV*(dst: HPDF_Destination; left: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_GetEncoder*(pdf: HPDF_Doc; encoding_name: HPDF_PCHAR): HPDF_Encoder; 
PROCEDURE ["StdCall"] / HPDF_GetCurrentEncoder*(pdf: HPDF_Doc): HPDF_Encoder;
PROCEDURE ["StdCall"] / HPDF_SetCurrentEncoder*(pdf: HPDF_Doc; encoding_name: HPDF_PCHAR): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Encoder_GetType*(encoder: HPDF_Encoder): THPDF_EncoderType;
PROCEDURE ["StdCall"] / HPDF_Encoder_GetByteType*(encoder: HPDF_Encoder; text: HPDF_PCHAR; index: HPDF_UINT): THPDF_ByteType; 
PROCEDURE ["StdCall"] / HPDF_Encoder_GetUnicode*(encoder: HPDF_Encoder; code: HPDF_UINT16): HPDF_UNICODE; 
PROCEDURE ["StdCall"] / HPDF_Encoder_GetWritingMode*(encoder: HPDF_Encoder): THPDF_EncoderType;
PROCEDURE ["StdCall"] / HPDF_UseJPEncodings*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_UseKREncodings*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_UseCNSEncodings*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_UseCNTEncodings*(pdf: HPDF_Doc): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_CreateTextAnnot*(page: HPDF_Page; rect: THPDF_Rect;text: HPDF_PCHAR; encoder: HPDF_Encoder): HPDF_Annotation;
PROCEDURE ["StdCall"] / HPDF_Page_CreateLinkAnnot*(page: HPDF_Page; rect: THPDF_Rect; dst: HPDF_Destination): HPDF_Annotation;
PROCEDURE ["StdCall"] / HPDF_Page_CreateURILinkAnnot*(page: HPDF_Page; rect: THPDF_Rect; uri: HPDF_PCHAR): HPDF_Annotation; 
PROCEDURE ["StdCall"] / HPDF_LinkAnnot_SetHighlightMode*(annot: HPDF_Annotation; mode: THPDF_AnnotHighlightMode): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_LinkAnnot_SetBorderStyle*(annot: HPDF_Annotation; width: HPDF_REAL; dash_on: HPDF_UINT16; dash_off: HPDF_UINT16): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_TextAnnot_SetIcon*(annot: HPDF_Annotation; icon: THPDF_AnnotIcon): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_TextAnnot_SetOpened*(annot: HPDF_Annotation; opened: HPDF_BOOL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_LoadPngImageFromFile*(pdf: HPDF_Doc; filename: HPDF_PCHAR): HPDF_Image; 
PROCEDURE ["StdCall"] / HPDF_LoadPngImageFromFile2*(pdf: HPDF_Doc; filename: HPDF_PCHAR): HPDF_Image; 
PROCEDURE ["StdCall"] / HPDF_LoadJpegImageFromFile*(pdf: HPDF_Doc; filename: HPDF_PCHAR): HPDF_Image; 
PROCEDURE ["StdCall"] / HPDF_LoadRawImageFromFile*(pdf: HPDF_Doc; filename: HPDF_PCHAR; width: HPDF_UINT; height: HPDF_UINT; color_space: THPDF_ColorSpace): HPDF_Image; 
PROCEDURE ["StdCall"] / HPDF_LoadRawImageFromMem*(pdf: HPDF_Doc; buf: HPDF_PBYTE; width: HPDF_UINT; height: HPDF_UINT; color_space: THPDF_ColorSpace; bits_per_component: HPDF_UINT): HPDF_Image; 
PROCEDURE ["StdCall"] / HPDF_Image_GetSize2*(image: HPDF_Image; VAR size: THPDF_Point): HPDF_STATUS;
--PROCEDURE ["StdCall"] / HPDF_Image_GetSize*(image: HPDF_Image): THPDF_Point;
PROCEDURE ["StdCall"] / HPDF_Image_GetWidth*(image: HPDF_Image): HPDF_UINT;
PROCEDURE ["StdCall"] / HPDF_Image_GetHeight*(image: HPDF_Image): HPDF_UINT;
PROCEDURE ["StdCall"] / HPDF_Image_GetBitsPerComponent*(image: HPDF_Image): HPDF_UINT;
PROCEDURE ["StdCall"] / HPDF_Image_GetColorSpace*(image: HPDF_Image): HPDF_PCHAR;
PROCEDURE ["StdCall"] / HPDF_Image_SetColorMask*(image: HPDF_Image; rmin: HPDF_UINT; rmax: HPDF_UINT; gmin: HPDF_UINT; gmax: HPDF_UINT; bmin: HPDF_UINT; bmax: HPDF_UINT): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Image_SetMaskImage*(image: HPDF_Image; mask_image: HPDF_Image): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_SetInfoAttr*(pdf: HPDF_Doc; info_type: THPDF_InfoType; value: HPDF_PCHAR): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_GetInfoAttr*(pdf: HPDF_Doc; info_type: THPDF_InfoType): HPDF_PCHAR;
PROCEDURE ["StdCall"] / HPDF_SetInfoDateAttr*(pdf: HPDF_Doc; info_type: THPDF_InfoType; value: THPDF_Date): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_SetPassword*(pdf: HPDF_Doc; owner_passwd: HPDF_PCHAR; user_passwd: HPDF_PCHAR): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_SetPermission*(pdf: HPDF_Doc; permission: HPDF_UINT): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_SetEncryptionMode*(pdf: HPDF_Doc; mode: THPDF_EncryptMode; key_len: HPDF_UINT): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_SetCompressionMode*(pdf: HPDF_Doc; mode: TCompressionMode): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_TextWidth*(page: HPDF_Page; text: HPDF_PCHAR): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_MeasureText*(page: HPDF_Page; text: HPDF_PCHAR; width: HPDF_REAL; wordwrap: HPDF_BOOL; VAR real_width: HPDF_REAL): HPDF_UINT;
PROCEDURE ["StdCall"] / HPDF_Page_GetWidth*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetHeight*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetGMode*(page: HPDF_Page): HPDF_UINT16;
PROCEDURE ["StdCall"] / HPDF_Page_GetCurrentPos2*(page: HPDF_Page; VAR pos: THPDF_Point): HPDF_STATUS;
--PROCEDURE ["StdCall"] / HPDF_Page_GetCurrentPos*(page: HPDF_Page): THPDF_Point;
PROCEDURE ["StdCall"] / HPDF_Page_GetCurrentTextPos2*(page: HPDF_Page; VAR pos: THPDF_Point): HPDF_STATUS;
--PROCEDURE ["StdCall"] / HPDF_Page_GetCurrentTextPos*(page: HPDF_Page): THPDF_Point;
PROCEDURE ["StdCall"] / HPDF_Page_GetCurrentFont*(page: HPDF_Page): HPDF_Font;
PROCEDURE ["StdCall"] / HPDF_Font_GetFontName*(font: HPDF_Font): HPDF_PCHAR;
PROCEDURE ["StdCall"] / HPDF_Font_GetEncodingName*(font: HPDF_Font): HPDF_PCHAR;
PROCEDURE ["StdCall"] / HPDF_Font_GetUnicodeWidth*(font: HPDF_Font; code: HPDF_UNICODE): HPDF_INT;
PROCEDURE ["StdCall"] / HPDF_Font_GetBBox*(font: HPDF_Font): THPDF_Rect;
PROCEDURE ["StdCall"] / HPDF_Font_GetAscent*(font: HPDF_Font): HPDF_INT;
PROCEDURE ["StdCall"] / HPDF_Font_GetDescent*(font: HPDF_Font): HPDF_INT;
PROCEDURE ["StdCall"] / HPDF_Font_GetXHeight*(font: HPDF_Font): HPDF_UINT;
PROCEDURE ["StdCall"] / HPDF_Font_GetCapHeight*(font: HPDF_Font): HPDF_UINT;
PROCEDURE ["StdCall"] / HPDF_Font_TextWidth*(font: HPDF_Font; text: HPDF_PCHAR; len: HPDF_UINT): THPDF_TextWidth;
PROCEDURE ["StdCall"] / HPDF_Font_MeasureText*(font: HPDF_Font; text: HPDF_PCHAR; len: HPDF_UINT; width: HPDF_REAL; font_size: HPDF_REAL; char_space: HPDF_REAL;  word_space: HPDF_REAL; wordwrap: HPDF_BOOL; VAR real_width: HPDF_REAL) : HPDF_UINT;
PROCEDURE ["StdCall"] / HPDF_CreateExtGState*(pdf: HPDF_Doc) : HPDF_ExtGState;
PROCEDURE ["StdCall"] / HPDF_ExtGState_SetAlphaStroke*(ext_gstate: HPDF_ExtGState; value: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_ExtGState_SetAlphaFill*(ext_gstate: HPDF_ExtGState; value: HPDF_REAL) : HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_ExtGState_SetBlendMode*(ext_gstate: HPDF_ExtGState; mode: THPDF_BlendMode) : HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_GetCurrentFontSize*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetTransMatrix*(page: HPDF_Page): THPDF_TransMatrix;
PROCEDURE ["StdCall"] / HPDF_Page_GetLineWidth*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetLineCap*(page: HPDF_Page): THPDF_LineCap;
PROCEDURE ["StdCall"] / HPDF_Page_GetLineJoin*(page: HPDF_Page): THPDF_LineJoin;
PROCEDURE ["StdCall"] / HPDF_Page_GetMiterLimit*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetDash*(page: HPDF_Page): THPDF_DashMode;
PROCEDURE ["StdCall"] / HPDF_Page_GetFlat*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetCharSpace*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetWordSpace*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetHorizontalScalling*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetTextLeading*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetTextRenderingMode*(page: HPDF_Page): THPDF_TextRenderingMode;
PROCEDURE ["StdCall"] / HPDF_Page_GetTextRaise*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetRGBFill*(page: HPDF_Page): THPDF_RGBColor;
PROCEDURE ["StdCall"] / HPDF_Page_GetRGBStroke*(page: HPDF_Page): THPDF_RGBColor;
PROCEDURE ["StdCall"] / HPDF_Page_GetCMYKFill*(page: HPDF_Page): THPDF_CMYKColor;
PROCEDURE ["StdCall"] / HPDF_Page_GetCMYKStroke*(page: HPDF_Page): THPDF_CMYKColor;
PROCEDURE ["StdCall"] / HPDF_Page_GetGrayFill*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetGrayStroke*(page: HPDF_Page): HPDF_REAL;
PROCEDURE ["StdCall"] / HPDF_Page_GetStrokingColorSpace*(page: HPDF_Page): THPDF_ColorSpace;
PROCEDURE ["StdCall"] / HPDF_Page_GetFillingColorSpace*(page: HPDF_Page): THPDF_ColorSpace;
PROCEDURE ["StdCall"] / HPDF_Page_GetTextMatrix*(page: HPDF_Page): THPDF_TransMatrix;
PROCEDURE ["StdCall"] / HPDF_Page_GetGStateDepth*(page: HPDF_Page): HPDF_UINT;
PROCEDURE ["StdCall"] / HPDF_Page_SetLineWidth*(page: HPDF_Page; line_width: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetLineCap*(page: HPDF_Page; line_cap: THPDF_LineCap): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetLineJoin*(page: HPDF_Page; line_join: THPDF_LineJoin): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetMiterLimit*(page: HPDF_Page; miter_limit: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetDash*(page: HPDF_Page; ptn: HPDF_PUINT16; num_param: HPDF_UINT; phase: HPDF_UINT): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetFlat*(page: HPDF_Page; flatness: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetExtGState*(page: HPDF_Page; ext_gstate: HPDF_ExtGState): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_GSave*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_GRestore*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_Concat*(page: HPDF_Page; a: HPDF_REAL; b: HPDF_REAL; c: HPDF_REAL; d: HPDF_REAL; x: HPDF_REAL; y: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_MoveTo*(page: HPDF_Page; x: HPDF_REAL; y: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_LineTo*(page: HPDF_Page; x: HPDF_REAL; y: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_CurveTo*(page: HPDF_Page; x1: HPDF_REAL; y1: HPDF_REAL; x2: HPDF_REAL; y2: HPDF_REAL; x3: HPDF_REAL; y3: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_CurveTo2*(page: HPDF_Page; x2: HPDF_REAL; y2: HPDF_REAL; x3: HPDF_REAL; y3: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_CurveTo3*(page: HPDF_Page; x1: HPDF_REAL; y1: HPDF_REAL; x3: HPDF_REAL; y3: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_ClosePath*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_Rectangle*(page: HPDF_Page; x: HPDF_REAL; y: HPDF_REAL; width: HPDF_REAL; height: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_Stroke*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_ClosePathStroke*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_Fill*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_Eofill*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_FillStroke*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_EofillStroke*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_ClosePathFillStroke*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_ClosePathEofillStroke*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_EndPath*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_Clip*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_Eoclip*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_BeginText*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_EndText*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetCharSpace*(page: HPDF_Page; value: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetWordSpace*(page: HPDF_Page; value: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetHorizontalScalling*(page: HPDF_Page; value: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetTextLeading*(page: HPDF_Page; value: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetFontAndSize*(page: HPDF_Page; font: HPDF_Font; size: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetTextRenderingMode*(page: HPDF_Page; mode: THPDF_TextRenderingMode): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetTextRaise*(page: HPDF_Page; value: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_MoveTextPos*(page: HPDF_Page; x: HPDF_REAL; y: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_MoveTextPos2*(page: HPDF_Page; x: HPDF_REAL; y: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetTextMatrix*(page: HPDF_Page; a: HPDF_REAL; b: HPDF_REAL; c: HPDF_REAL; d: HPDF_REAL; x: HPDF_REAL; y: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_MoveToNextLine*(page: HPDF_Page): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_ShowText*(page: HPDF_Page; text: HPDF_PCHAR): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_ShowTextNextLine*(page: HPDF_Page; text: HPDF_PCHAR): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_ShowTextNextLineEx*(page: HPDF_Page; word_space: HPDF_REAL; char_space: HPDF_REAL; text: HPDF_PCHAR): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetGrayFill*(page: HPDF_Page; gray: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetGrayStroke*(page: HPDF_Page; gray: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_SetRGBFill*(page: HPDF_Page; r: HPDF_REAL; g: HPDF_REAL;b: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetRGBStroke*(page: HPDF_Page; r: HPDF_REAL; g: HPDF_REAL;b: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetCMYKFill*(page: HPDF_Page; c: HPDF_REAL; m: HPDF_REAL; y: HPDF_REAL; k: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetCMYKStroke*(page: HPDF_Page; c: HPDF_REAL; m: HPDF_REAL; y: HPDF_REAL; k: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_ExecuteXObject*(page: HPDF_Page; obj: HPDF_XObject): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_DrawImage*(page: HPDF_Page; image: HPDF_Image; x: HPDF_REAL; y: HPDF_REAL; width: HPDF_REAL; height: HPDF_REAL): HPDF_STATUS;
PROCEDURE ["StdCall"] / HPDF_Page_Circle*(page: HPDF_Page; x: HPDF_REAL; y: HPDF_REAL; ray: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_Arc*(page: HPDF_Page; x: HPDF_REAL; y: HPDF_REAL; ray: HPDF_REAL; ang1: HPDF_REAL; ang2: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_Ellipse*(page: HPDF_Page; x: HPDF_REAL; y: HPDF_REAL; xray: HPDF_REAL; yray: HPDF_REAL): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_TextOut*(page: HPDF_Page; xpos: HPDF_REAL; ypos: HPDF_REAL; text: HPDF_PCHAR): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_TextRect*(page: HPDF_Page; left: HPDF_REAL; top: HPDF_REAL; right: HPDF_REAL; bottom: HPDF_REAL; text: HPDF_PCHAR; align: THPDF_TextAlignment; len: HPDF_PUINT): HPDF_STATUS; 
PROCEDURE ["StdCall"] / HPDF_Page_SetSlideShow*(page: HPDF_Page; sstype: THPDF_TransitionStyle; disp_time: HPDF_REAL; trans_time: HPDF_REAL): HPDF_STATUS; 
         

PROCEDURE HPDF_Page_GetCurrentPos*(page: HPDF_Page): THPDF_Point;
VAR
  pos: THPDF_Point;
BEGIN
  HPDF_Page_GetCurrentPos2(page, pos);
  RETURN pos;
END HPDF_Page_GetCurrentPos;

PROCEDURE HPDF_Page_GetCurrentTextPos*(page: HPDF_Page): THPDF_Point;
VAR
  pos: THPDF_Point;
BEGIN
  HPDF_Page_GetCurrentTextPos2(page, pos);
  RETURN pos;
END HPDF_Page_GetCurrentTextPos;

PROCEDURE HPDF_Image_GetSize*(image: HPDF_Image): THPDF_Point;
VAR 
  size: THPDF_Point;
BEGIN
  HPDF_Image_GetSize2(image, size);
  RETURN size;
END HPDF_Image_GetSize;

END hpdf.

