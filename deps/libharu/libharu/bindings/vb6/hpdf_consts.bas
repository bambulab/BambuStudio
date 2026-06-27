Attribute VB_Name = "Module2"
''/*
' * << Haru Free PDF Library 2.0.3 >> -- hpdf_consts.h
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


'#ifndef _HPDF_CONSTS_H
'public const _HPDF_CONSTS_H

''/*----------------------------------------------------------------------------*/

Public Const HPDF_TRUE = 1
Public Const HPDF_FALSE = 0

Public Const HPDF_OK = 0
Public Const HPDF_NOERROR = 0

'/*----- default values -------------------------------------------------------*/

'/* buffer size which is required when we convert to character string. */
Public Const HPDF_TMP_BUF_SIZ = 256
Public Const HPDF_SHORT_BUF_SIZ = 32
Public Const HPDF_REAL_LEN = 11
Public Const HPDF_INT_LEN = 11
Public Const HPDF_TEXT_DEFAULT_LEN = 256
Public Const HPDF_UNICODE_HEADER_LEN = 2
Public Const HPDF_DATE_TIME_STR_LEN = 23

'/* length of each item defined in PDF */
Public Const HPDF_BYTE_OFFSET_LEN = 10
Public Const HPDF_OBJ_ID_LEN = 7
Public Const HPDF_GEN_NO_LEN = 5

'/* default value of Graphic State */
Public Const HPDF_DEF_FONT = "Helvetica"
Public Const HPDF_DEF_PAGE_LAYOUT = HPDF_PAGE_LAYOUT_SINGLE
Public Const HPDF_DEF_PAGE_MODE = HPDF_PAGE_MODE_USE_NONE
Public Const HPDF_DEF_WORDSPACE = 0
Public Const HPDF_DEF_CHARSPACE = 0
Public Const HPDF_DEF_FONTSIZE = 10
Public Const HPDF_DEF_HSCALING = 100
Public Const HPDF_DEF_LEADING = 0
Public Const HPDF_DEF_RENDERING_MODE = HPDF_FILL
Public Const HPDF_DEF_RISE = 0
Public Const HPDF_DEF_RAISE = HPDF_DEF_RISE
Public Const HPDF_DEF_LINEWIDTH = 1
Public Const HPDF_DEF_LINECAP = HPDF_BUTT_END
Public Const HPDF_DEF_LINEJOIN = HPDF_MITER_JOIN
Public Const HPDF_DEF_MITERLIMIT = 10
Public Const HPDF_DEF_FLATNESS = 1
Public Const HPDF_DEF_PAGE_NUM = 1

Public Const HPDF_BS_DEF_WIDTH = 1

'/* default page-size */
Public Const HPDF_DEF_PAGE_WIDTH = 595.276
Public Const HPDF_DEF_PAGE_HEIGHT = 841.89

Public Const HPDF_VERSION_TEXT = "2.0.5"

'/*---------------------------------------------------------------------------*/
'/*----- compression mode ----------------------------------------------------*/

Public Const HPDF_COMP_NONE = &H0
Public Const HPDF_COMP_TEXT = &H1
Public Const HPDF_COMP_IMAGE = &H2
Public Const HPDF_COMP_METADATA = &H4
Public Const HPDF_COMP_ALL = &HF
'/* public const  HPDF_COMP_BEST_COMPRESS   =&H10
' * public const  HPDF_COMP_BEST_SPEED      =&H20
' */
Public Const HPDF_COMP_MASK = &HFF


'/*----------------------------------------------------------------------------*/
'/*----- permission flags (only Revision 2 is supported)-----------------------*/

Public Const HPDF_ENABLE_READ = 0
Public Const HPDF_ENABLE_PRINT = 4
Public Const HPDF_ENABLE_EDIT_ALL = 8
Public Const HPDF_ENABLE_COPY = 16
Public Const HPDF_ENABLE_EDIT = 32


'/*----------------------------------------------------------------------------*/
'/*------ viewer preferences definitions --------------------------------------*/

Public Const HPDF_HIDE_TOOLBAR = 1
Public Const HPDF_HIDE_MENUBAR = 2
Public Const HPDF_HIDE_WINDOW_UI = 4
Public Const HPDF_FIT_WINDOW = 8
Public Const HPDF_CENTER_WINDOW = 16


'/*---------------------------------------------------------------------------*/
'/*------ limitation of object implementation (PDF1.4) -----------------------*/

Public Const HPDF_LIMIT_MAX_INT = 2147483647
Public Const HPDF_LIMIT_MIN_INT = -2147483647

Public Const HPDF_LIMIT_MAX_REAL = 32767
Public Const HPDF_LIMIT_MIN_REAL = -32767

Public Const HPDF_LIMIT_MAX_STRING_LEN = 65535
Public Const HPDF_LIMIT_MAX_NAME_LEN = 127

Public Const HPDF_LIMIT_MAX_ARRAY = 8191
Public Const HPDF_LIMIT_MAX_DICT_ELEMENT = 4095
Public Const HPDF_LIMIT_MAX_XREF_ELEMENT = 8388607
Public Const HPDF_LIMIT_MAX_GSTATE = 28
Public Const HPDF_LIMIT_MAX_DEVICE_N = 8
Public Const HPDF_LIMIT_MAX_DEVICE_N_V15 = 32
Public Const HPDF_LIMIT_MAX_CID = 65535
Public Const HPDF_MAX_GENERATION_NUM = 65535

Public Const HPDF_MIN_PAGE_HEIGHT = 3
Public Const HPDF_MIN_PAGE_WIDTH = 3
Public Const HPDF_MAX_PAGE_HEIGHT = 14400
Public Const HPDF_MAX_PAGE_WIDTH = 14400
Public Const HPDF_MIN_MAGNIFICATION_FACTOR = 8
Public Const HPDF_MAX_MAGNIFICATION_FACTOR = 3200

'/*---------------------------------------------------------------------------*/
'/*------ limitation of various properties -----------------------------------*/

Public Const HPDF_MIN_PAGE_SIZE = 3
Public Const HPDF_MAX_PAGE_SIZE = 14400
Public Const HPDF_MIN_HORIZONTALSCALING = 10
Public Const HPDF_MAX_HORIZONTALSCALING = 300
Public Const HPDF_MIN_WORDSPACE = -30
Public Const HPDF_MAX_WORDSPACE = 300
Public Const HPDF_MIN_CHARSPACE = -30
Public Const HPDF_MAX_CHARSPACE = 300
Public Const HPDF_MAX_FONTSIZE = 300
Public Const HPDF_MAX_ZOOMSIZE = 10
Public Const HPDF_MAX_LEADING = 300
Public Const HPDF_MAX_LINEWIDTH = 100
Public Const HPDF_MAX_DASH_PATTERN = 100

Public Const HPDF_MAX_JWW_NUM = 128

'/*----------------------------------------------------------------------------*/
'/*----- country code definition ----------------------------------------------*/

Public Const HPDF_COUNTRY_AF = "AF"    '/* AFGHANISTAN */
Public Const HPDF_COUNTRY_AL = "AL"    '/* ALBANIA */
Public Const HPDF_COUNTRY_DZ = "DZ"    '/* ALGERIA */
Public Const HPDF_COUNTRY_AS = "AS"    '/* AMERICAN SAMOA */
Public Const HPDF_COUNTRY_AD = "AD"    '/* ANDORRA */
Public Const HPDF_COUNTRY_AO = "AO"    '/* ANGOLA */
Public Const HPDF_COUNTRY_AI = "AI"    '/* ANGUILLA */
Public Const HPDF_COUNTRY_AQ = "AQ"    '/* ANTARCTICA */
Public Const HPDF_COUNTRY_AG = "AG"    '/* ANTIGUA AND BARBUDA */
Public Const HPDF_COUNTRY_AR = "AR"    '/* ARGENTINA */
Public Const HPDF_COUNTRY_AM = "AM"    '/* ARMENIA */
Public Const HPDF_COUNTRY_AW = "AW"    '/* ARUBA */
Public Const HPDF_COUNTRY_AU = "AU"    '/* AUSTRALIA */
Public Const HPDF_COUNTRY_AT = "AT"    '/* AUSTRIA */
Public Const HPDF_COUNTRY_AZ = "AZ"    '/* AZERBAIJAN */
Public Const HPDF_COUNTRY_BS = "BS"    '/* BAHAMAS */
Public Const HPDF_COUNTRY_BH = "BH"    '/* BAHRAIN */
Public Const HPDF_COUNTRY_BD = "BD"    '/* BANGLADESH */
Public Const HPDF_COUNTRY_BB = "BB"    '/* BARBADOS */
Public Const HPDF_COUNTRY_BY = "BY"    '/* BELARUS */
Public Const HPDF_COUNTRY_BE = "BE"    '/* BELGIUM */
Public Const HPDF_COUNTRY_BZ = "BZ"    '/* BELIZE */
Public Const HPDF_COUNTRY_BJ = "BJ"    '/* BENIN */
Public Const HPDF_COUNTRY_BM = "BM"    '/* BERMUDA */
Public Const HPDF_COUNTRY_BT = "BT"    '/* BHUTAN */
Public Const HPDF_COUNTRY_BO = "BO"    '/* BOLIVIA */
Public Const HPDF_COUNTRY_BA = "BA"    '/* BOSNIA AND HERZEGOWINA */
Public Const HPDF_COUNTRY_BW = "BW"    '/* BOTSWANA */
Public Const HPDF_COUNTRY_BV = "BV"    '/* BOUVET ISLAND */
Public Const HPDF_COUNTRY_BR = "BR"    '/* BRAZIL */
Public Const HPDF_COUNTRY_IO = "IO"    '/* BRITISH INDIAN OCEAN TERRITORY */
Public Const HPDF_COUNTRY_BN = "BN"    '/* BRUNEI DARUSSALAM */
Public Const HPDF_COUNTRY_BG = "BG"    '/* BULGARIA */
Public Const HPDF_COUNTRY_BF = "BF"    '/* BURKINA FASO */
Public Const HPDF_COUNTRY_BI = "BI"    '/* BURUNDI */
Public Const HPDF_COUNTRY_KH = "KH"    '/* CAMBODIA */
Public Const HPDF_COUNTRY_CM = "CM"    '/* CAMEROON */
Public Const HPDF_COUNTRY_CA = "CA"    '/* CANADA */
Public Const HPDF_COUNTRY_CV = "CV"    '/* CAPE VERDE */
Public Const HPDF_COUNTRY_KY = "KY"    '/* CAYMAN ISLANDS */
Public Const HPDF_COUNTRY_CF = "CF"    '/* CENTRAL AFRICAN REPUBLIC */
Public Const HPDF_COUNTRY_TD = "TD"    '/* CHAD */
Public Const HPDF_COUNTRY_CL = "CL"    '/* CHILE */
Public Const HPDF_COUNTRY_CN = "CN"    '/* CHINA */
Public Const HPDF_COUNTRY_CX = "CX"    '/* CHRISTMAS ISLAND */
Public Const HPDF_COUNTRY_CC = "CC"    '/* COCOS (KEELING) ISLANDS */
Public Const HPDF_COUNTRY_CO = "CO"    '/* COLOMBIA */
Public Const HPDF_COUNTRY_KM = "KM"    '/* COMOROS */
Public Const HPDF_COUNTRY_CG = "CG"    '/* CONGO */
Public Const HPDF_COUNTRY_CK = "CK"    '/* COOK ISLANDS */
Public Const HPDF_COUNTRY_CR = "CR"    '/* COSTA RICA */
Public Const HPDF_COUNTRY_CI = "CI"    '/* COTE D'IVOIRE */
Public Const HPDF_COUNTRY_HR = "HR"    '/* CROATIA (local name: Hrvatska) */
Public Const HPDF_COUNTRY_CU = "CU"    '/* CUBA */
Public Const HPDF_COUNTRY_CY = "CY"    '/* CYPRUS */
Public Const HPDF_COUNTRY_CZ = "CZ"    '/* CZECH REPUBLIC */
Public Const HPDF_COUNTRY_DK = "DK"    '/* DENMARK */
Public Const HPDF_COUNTRY_DJ = "DJ"    '/* DJIBOUTI */
Public Const HPDF_COUNTRY_DM = "DM"    '/* DOMINICA */
Public Const HPDF_COUNTRY_DO = "DO"    '/* DOMINICAN REPUBLIC */
Public Const HPDF_COUNTRY_TP = "TP"    '/* EAST TIMOR */
Public Const HPDF_COUNTRY_EC = "EC"    '/* ECUADOR */
Public Const HPDF_COUNTRY_EG = "EG"    '/* EGYPT */
Public Const HPDF_COUNTRY_SV = "SV"    '/* EL SALVADOR */
Public Const HPDF_COUNTRY_GQ = "GQ"    '/* EQUATORIAL GUINEA */
Public Const HPDF_COUNTRY_ER = "ER"    '/* ERITREA */
Public Const HPDF_COUNTRY_EE = "EE"    '/* ESTONIA */
Public Const HPDF_COUNTRY_ET = "ET"    '/* ETHIOPIA */
Public Const HPDF_COUNTRY_FK = "FK"   '/* FALKLAND ISLANDS (MALVINAS) */
Public Const HPDF_COUNTRY_FO = "FO"    '/* FAROE ISLANDS */
Public Const HPDF_COUNTRY_FJ = "FJ"    '/* FIJI */
Public Const HPDF_COUNTRY_FI = "FI"    '/* FINLAND */
Public Const HPDF_COUNTRY_FR = "FR"    '/* FRANCE */
Public Const HPDF_COUNTRY_FX = "FX"    '/* FRANCE, METROPOLITAN */
Public Const HPDF_COUNTRY_GF = "GF"    '/* FRENCH GUIANA */
Public Const HPDF_COUNTRY_PF = "PF"    '/* FRENCH POLYNESIA */
Public Const HPDF_COUNTRY_TF = "TF"    '/* FRENCH SOUTHERN TERRITORIES */
Public Const HPDF_COUNTRY_GA = "GA"    '/* GABON */
Public Const HPDF_COUNTRY_GM = "GM"    '/* GAMBIA */
Public Const HPDF_COUNTRY_GE = "GE"    '/* GEORGIA */
Public Const HPDF_COUNTRY_DE = "DE"    '/* GERMANY */
Public Const HPDF_COUNTRY_GH = "GH"    '/* GHANA */
Public Const HPDF_COUNTRY_GI = "GI"    '/* GIBRALTAR */
Public Const HPDF_COUNTRY_GR = "GR"    '/* GREECE */
Public Const HPDF_COUNTRY_GL = "GL"    '/* GREENLAND */
Public Const HPDF_COUNTRY_GD = "GD"    '/* GRENADA */
Public Const HPDF_COUNTRY_GP = "GP"    '/* GUADELOUPE */
Public Const HPDF_COUNTRY_GU = "GU"    '/* GUAM */
Public Const HPDF_COUNTRY_GT = "GT"    '/* GUATEMALA */
Public Const HPDF_COUNTRY_GN = "GN"    '/* GUINEA */
Public Const HPDF_COUNTRY_GW = "GW"    '/* GUINEA-BISSAU */
Public Const HPDF_COUNTRY_GY = "GY"    '/* GUYANA */
Public Const HPDF_COUNTRY_HT = "HT"    '/* HAITI */
Public Const HPDF_COUNTRY_HM = "HM"    '/* HEARD AND MC DONALD ISLANDS */
Public Const HPDF_COUNTRY_HN = "HN"    '/* HONDURAS */
Public Const HPDF_COUNTRY_HK = "HK"    '/* HONG KONG */
Public Const HPDF_COUNTRY_HU = "HU"    '/* HUNGARY */
Public Const HPDF_COUNTRY_IS = "IS"    '/* ICELAND */
Public Const HPDF_COUNTRY_IN = "IN"    '/* INDIA */
Public Const HPDF_COUNTRY_ID = "ID"    '/* INDONESIA */
Public Const HPDF_COUNTRY_IR = "IR"    '/* IRAN (ISLAMIC REPUBLIC OF) */
Public Const HPDF_COUNTRY_IQ = "IQ"    '/* IRAQ */
Public Const HPDF_COUNTRY_IE = "IE"    '/* IRELAND */
Public Const HPDF_COUNTRY_IL = "IL"    '/* ISRAEL */
Public Const HPDF_COUNTRY_IT = "IT"    '/* ITALY */
Public Const HPDF_COUNTRY_JM = "JM"    '/* JAMAICA */
Public Const HPDF_COUNTRY_JP = "JP"    '/* JAPAN */
Public Const HPDF_COUNTRY_JO = "JO"    '/* JORDAN */
Public Const HPDF_COUNTRY_KZ = "KZ"    '/* KAZAKHSTAN */
Public Const HPDF_COUNTRY_KE = "KE"    '/* KENYA */
Public Const HPDF_COUNTRY_KI = "KI"    '/* KIRIBATI */
Public Const HPDF_COUNTRY_KP = "KP"    '/* KOREA, DEMOCRATIC PEOPLE'S REPUBLIC OF */
Public Const HPDF_COUNTRY_KR = "KR"    '/* KOREA, REPUBLIC OF */
Public Const HPDF_COUNTRY_KW = "KW"    '/* KUWAIT */
Public Const HPDF_COUNTRY_KG = "KG"    '/* KYRGYZSTAN */
Public Const HPDF_COUNTRY_LA = "LA"    '/* LAO PEOPLE'S DEMOCRATIC REPUBLIC */
Public Const HPDF_COUNTRY_LV = "LV"    '/* LATVIA */
Public Const HPDF_COUNTRY_LB = "LB"    '/* LEBANON */
Public Const HPDF_COUNTRY_LS = "LS"    '/* LESOTHO */
Public Const HPDF_COUNTRY_LR = "LR"    '/* LIBERIA */
Public Const HPDF_COUNTRY_LY = "LY"    '/* LIBYAN ARAB JAMAHIRIYA */
Public Const HPDF_COUNTRY_LI = "LI"    '/* LIECHTENSTEIN */
Public Const HPDF_COUNTRY_LT = "LT"    '/* LITHUANIA */
Public Const HPDF_COUNTRY_LU = "LU"    '/* LUXEMBOURG */
Public Const HPDF_COUNTRY_MO = "MO"    '/* MACAU */
Public Const HPDF_COUNTRY_MK = "MK"   '/* MACEDONIA, THE FORMER YUGOSLAV REPUBLIC OF */
Public Const HPDF_COUNTRY_MG = "MG"    '/* MADAGASCAR */
Public Const HPDF_COUNTRY_MW = "MW"    '/* MALAWI */
Public Const HPDF_COUNTRY_MY = "MY"    '/* MALAYSIA */
Public Const HPDF_COUNTRY_MV = "MV"    '/* MALDIVES */
Public Const HPDF_COUNTRY_ML = "ML"    '/* MALI */
Public Const HPDF_COUNTRY_MT = "MT"    '/* MALTA */
Public Const HPDF_COUNTRY_MH = "MH"    '/* MARSHALL ISLANDS */
Public Const HPDF_COUNTRY_MQ = "MQ"    '/* MARTINIQUE */
Public Const HPDF_COUNTRY_MR = "MR"    '/* MAURITANIA */
Public Const HPDF_COUNTRY_MU = "MU"    '/* MAURITIUS */
Public Const HPDF_COUNTRY_YT = "YT"    '/* MAYOTTE */
Public Const HPDF_COUNTRY_MX = "MX"    '/* MEXICO */
Public Const HPDF_COUNTRY_FM = "FM"    '/* MICRONESIA, FEDERATED STATES OF */
Public Const HPDF_COUNTRY_MD = "MD"    '/* MOLDOVA, REPUBLIC OF */
Public Const HPDF_COUNTRY_MC = "MC"    '/* MONACO */
Public Const HPDF_COUNTRY_MN = "MN"    '/* MONGOLIA */
Public Const HPDF_COUNTRY_MS = "MS"    '/* MONTSERRAT */
Public Const HPDF_COUNTRY_MA = "MA"    '/* MOROCCO */
Public Const HPDF_COUNTRY_MZ = "MZ"    '/* MOZAMBIQUE */
Public Const HPDF_COUNTRY_MM = "MM"    '/* MYANMAR */
Public Const HPDF_COUNTRY_NA = "NA"    '/* NAMIBIA */
Public Const HPDF_COUNTRY_NR = "NR"    '/* NAURU */
Public Const HPDF_COUNTRY_NP = "NP"    '/* NEPAL */
Public Const HPDF_COUNTRY_NL = "NL"    '/* NETHERLANDS */
Public Const HPDF_COUNTRY_AN = "AN"    '/* NETHERLANDS ANTILLES */
Public Const HPDF_COUNTRY_NC = "NC"    '/* NEW CALEDONIA */
Public Const HPDF_COUNTRY_NZ = "NZ"    '/* NEW ZEALAND */
Public Const HPDF_COUNTRY_NI = "NI"    '/* NICARAGUA */
Public Const HPDF_COUNTRY_NE = "NE"    '/* NIGER */
Public Const HPDF_COUNTRY_NG = "NG"    '/* NIGERIA */
Public Const HPDF_COUNTRY_NU = "NU"    '/* NIUE */
Public Const HPDF_COUNTRY_NF = "NF"    '/* NORFOLK ISLAND */
Public Const HPDF_COUNTRY_MP = "MP"    '/* NORTHERN MARIANA ISLANDS */
Public Const HPDF_COUNTRY_NO = "NO"    '/* NORWAY */
Public Const HPDF_COUNTRY_OM = "OM"    '/* OMAN */
Public Const HPDF_COUNTRY_PK = "PK"    '/* PAKISTAN */
Public Const HPDF_COUNTRY_PW = "PW"    '/* PALAU */
Public Const HPDF_COUNTRY_PA = "PA"    '/* PANAMA */
Public Const HPDF_COUNTRY_PG = "PG"    '/* PAPUA NEW GUINEA */
Public Const HPDF_COUNTRY_PY = "PY"    '/* PARAGUAY */
Public Const HPDF_COUNTRY_PE = "PE"    '/* PERU */
Public Const HPDF_COUNTRY_PH = "PH"    '/* PHILIPPINES */
Public Const HPDF_COUNTRY_PN = "PN"    '/* PITCAIRN */
Public Const HPDF_COUNTRY_PL = "PL"    '/* POLAND */
Public Const HPDF_COUNTRY_PT = "PT"    '/* PORTUGAL */
Public Const HPDF_COUNTRY_PR = "PR"    '/* PUERTO RICO */
Public Const HPDF_COUNTRY_QA = "QA"    '/* QATAR */
Public Const HPDF_COUNTRY_RE = "RE"    '/* REUNION */
Public Const HPDF_COUNTRY_RO = "RO"    '/* ROMANIA */
Public Const HPDF_COUNTRY_RU = "RU"    '/* RUSSIAN FEDERATION */
Public Const HPDF_COUNTRY_RW = "RW"    '/* RWANDA */
Public Const HPDF_COUNTRY_KN = "KN"    '/* SAINT KITTS AND NEVIS */
Public Const HPDF_COUNTRY_LC = "LC"    '/* SAINT LUCIA */
Public Const HPDF_COUNTRY_VC = "VC"    '/* SAINT VINCENT AND THE GRENADINES */
Public Const HPDF_COUNTRY_WS = "WS"    '/* SAMOA */
Public Const HPDF_COUNTRY_SM = "SM"    '/* SAN MARINO */
Public Const HPDF_COUNTRY_ST = "ST"    '/* SAO TOME AND PRINCIPE */
Public Const HPDF_COUNTRY_SA = "SA"    '/* SAUDI ARABIA */
Public Const HPDF_COUNTRY_SN = "SN"    '/* SENEGAL */
Public Const HPDF_COUNTRY_SC = "SC"    '/* SEYCHELLES */
Public Const HPDF_COUNTRY_SL = "SL"    '/* SIERRA LEONE */
Public Const HPDF_COUNTRY_SG = "SG"    '/* SINGAPORE */
Public Const HPDF_COUNTRY_SK = "SK"    '/* SLOVAKIA (Slovak Republic) */
Public Const HPDF_COUNTRY_SI = "SI"    '/* SLOVENIA */
Public Const HPDF_COUNTRY_SB = "SB"    '/* SOLOMON ISLANDS */
Public Const HPDF_COUNTRY_SO = "SO"    '/* SOMALIA */
Public Const HPDF_COUNTRY_ZA = "ZA"    '/* SOUTH AFRICA */
Public Const HPDF_COUNTRY_ES = "ES"    '/* SPAIN */
Public Const HPDF_COUNTRY_LK = "LK"    '/* SRI LANKA */
Public Const HPDF_COUNTRY_SH = "SH"    '/* ST. HELENA */
Public Const HPDF_COUNTRY_PM = "PM"    '/* ST. PIERRE AND MIQUELON */
Public Const HPDF_COUNTRY_SD = "SD"    '/* SUDAN */
Public Const HPDF_COUNTRY_SR = "SR"    '/* SURINAME */
Public Const HPDF_COUNTRY_SJ = "SJ"    '/* SVALBARD AND JAN MAYEN ISLANDS */
Public Const HPDF_COUNTRY_SZ = "SZ"    '/* SWAZILAND */
Public Const HPDF_COUNTRY_SE = "SE"    '/* SWEDEN */
Public Const HPDF_COUNTRY_CH = "CH"    '/* SWITZERLAND */
Public Const HPDF_COUNTRY_SY = "SY"    '/* SYRIAN ARAB REPUBLIC */
Public Const HPDF_COUNTRY_TW = "TW"    '/* TAIWAN */
Public Const HPDF_COUNTRY_TJ = "TJ"    '/* TAJIKISTAN */
Public Const HPDF_COUNTRY_TZ = "TZ"    '/* TANZANIA, UNITED REPUBLIC OF */
Public Const HPDF_COUNTRY_TH = "TH"    '/* THAILAND */
Public Const HPDF_COUNTRY_TG = "TG"    '/* TOGO */
Public Const HPDF_COUNTRY_TK = "TK"    '/* TOKELAU */
Public Const HPDF_COUNTRY_TO = "TO"    '/* TONGA */
Public Const HPDF_COUNTRY_TT = "TT"    '/* TRINIDAD AND TOBAGO */
Public Const HPDF_COUNTRY_TN = "TN"    '/* TUNISIA */
Public Const HPDF_COUNTRY_TR = "TR"    '/* TURKEY */
Public Const HPDF_COUNTRY_TM = "TM"    '/* TURKMENISTAN */
Public Const HPDF_COUNTRY_TC = "TC"    '/* TURKS AND CAICOS ISLANDS */
Public Const HPDF_COUNTRY_TV = "TV"    '/* TUVALU */
Public Const HPDF_COUNTRY_UG = "UG"    '/* UGANDA */
Public Const HPDF_COUNTRY_UA = "UA"    '/* UKRAINE */
Public Const HPDF_COUNTRY_AE = "AE"    '/* UNITED ARAB EMIRATES */
Public Const HPDF_COUNTRY_GB = "GB"    '/* UNITED KINGDOM */
Public Const HPDF_COUNTRY_US = "US"    '/* UNITED STATES */
Public Const HPDF_COUNTRY_UM = "UM"    '/* UNITED STATES MINOR OUTLYING ISLANDS */
Public Const HPDF_COUNTRY_UY = "UY"    '/* URUGUAY */
Public Const HPDF_COUNTRY_UZ = "UZ"    '/* UZBEKISTAN */
Public Const HPDF_COUNTRY_VU = "VU"    '/* VANUATU */
Public Const HPDF_COUNTRY_VA = "VA"    '/* VATICAN CITY STATE (HOLY SEE) */
Public Const HPDF_COUNTRY_VE = "VE"    '/* VENEZUELA */
Public Const HPDF_COUNTRY_VN = "VN"    '/* VIET NAM */
Public Const HPDF_COUNTRY_VG = "VG"    '/* VIRGIN ISLANDS (BRITISH) */
Public Const HPDF_COUNTRY_VI = "VI"    '/* VIRGIN ISLANDS (U.S.) */
Public Const HPDF_COUNTRY_WF = "WF"    '/* WALLIS AND FUTUNA ISLANDS */
Public Const HPDF_COUNTRY_EH = "EH"    '/* WESTERN SAHARA */
Public Const HPDF_COUNTRY_YE = "YE"    '/* YEMEN */
Public Const HPDF_COUNTRY_YU = "YU"    '/* YUGOSLAVIA */
Public Const HPDF_COUNTRY_ZR = "ZR"    '/* ZAIRE */
Public Const HPDF_COUNTRY_ZM = "ZM"    '/* ZAMBIA */
Public Const HPDF_COUNTRY_ZW = "ZW"    '/* ZIMBABWE */

'/*----------------------------------------------------------------------------*/
'/*----- lang code definition -------------------------------------------------*/

Public Const HPDF_LANG_AA = "aa"       '/* Afar */
Public Const HPDF_LANG_AB = "ab"       '/* Abkhazian */
Public Const HPDF_LANG_AF = "af"       '/* Afrikaans */
Public Const HPDF_LANG_AM = "am"       '/* Amharic */
Public Const HPDF_LANG_AR = "ar"       '/* Arabic */
Public Const HPDF_LANG_AS = "as"       '/* Assamese */
Public Const HPDF_LANG_AY = "ay"       '/* Aymara */
Public Const HPDF_LANG_AZ = "az"       '/* Azerbaijani */
Public Const HPDF_LANG_BA = "ba"       '/* Bashkir */
Public Const HPDF_LANG_BE = "be"       '/* Byelorussian */
Public Const HPDF_LANG_BG = "bg"       '/* Bulgarian */
Public Const HPDF_LANG_BH = "bh"       '/* Bihari */
Public Const HPDF_LANG_BI = "bi"       '/* Bislama */
Public Const HPDF_LANG_BN = "bn"       '/* Bengali Bangla */
Public Const HPDF_LANG_BO = "bo"       '/* Tibetan */
Public Const HPDF_LANG_BR = "br"       '/* Breton */
Public Const HPDF_LANG_CA = "ca"       '/* Catalan */
Public Const HPDF_LANG_CO = "co"       '/* Corsican */
Public Const HPDF_LANG_CS = "cs"       '/* Czech */
Public Const HPDF_LANG_CY = "cy"       '/* Welsh */
Public Const HPDF_LANG_DA = "da"       '/* Danish */
Public Const HPDF_LANG_DE = "de"       '/* German */
Public Const HPDF_LANG_DZ = "dz"       '/* Bhutani */
Public Const HPDF_LANG_EL = "el"       '/* Greek */
Public Const HPDF_LANG_EN = "en"       '/* English */
Public Const HPDF_LANG_EO = "eo"       '/* Esperanto */
Public Const HPDF_LANG_ES = "es"       '/* Spanish */
Public Const HPDF_LANG_ET = "et"       '/* Estonian */
Public Const HPDF_LANG_EU = "eu"       '/* Basque */
Public Const HPDF_LANG_FA = "fa"       '/* Persian */
Public Const HPDF_LANG_FI = "fi"       '/* Finnish */
Public Const HPDF_LANG_FJ = "fj"       '/* Fiji */
Public Const HPDF_LANG_FO = "fo"       '/* Faeroese */
Public Const HPDF_LANG_FR = "fr"       '/* French */
Public Const HPDF_LANG_FY = "fy"       '/* Frisian */
Public Const HPDF_LANG_GA = "ga"       '/* Irish */
Public Const HPDF_LANG_GD = "gd"       '/* Scots Gaelic */
Public Const HPDF_LANG_GL = "gl"       '/* Galician */
Public Const HPDF_LANG_GN = "gn"       '/* Guarani */
Public Const HPDF_LANG_GU = "gu"       '/* Gujarati */
Public Const HPDF_LANG_HA = "ha"       '/* Hausa */
Public Const HPDF_LANG_HI = "hi"       '/* Hindi */
Public Const HPDF_LANG_HR = "hr"       '/* Croatian */
Public Const HPDF_LANG_HU = "hu"       '/* Hungarian */
Public Const HPDF_LANG_HY = "hy"       '/* Armenian */
Public Const HPDF_LANG_IA = "ia"       '/* Interlingua */
Public Const HPDF_LANG_IE = "ie"       '/* Interlingue */
Public Const HPDF_LANG_IK = "ik"       '/* Inupiak */
Public Const HPDF_LANG_IN = "in"       '/* Indonesian */
Public Const HPDF_LANG_IS = "is"       '/* Icelandic */
Public Const HPDF_LANG_IT = "it"       '/* Italian */
Public Const HPDF_LANG_IW = "iw"       '/* Hebrew */
Public Const HPDF_LANG_JA = "ja"       '/* Japanese */
Public Const HPDF_LANG_JI = "ji"       '/* Yiddish */
Public Const HPDF_LANG_JW = "jw"       '/* Javanese */
Public Const HPDF_LANG_KA = "ka"       '/* Georgian */
Public Const HPDF_LANG_KK = "kk"       '/* Kazakh */
Public Const HPDF_LANG_KL = "kl"       '/* Greenlandic */
Public Const HPDF_LANG_KM = "km"       '/* Cambodian */
Public Const HPDF_LANG_KN = "kn"       '/* Kannada */
Public Const HPDF_LANG_KO = "ko"       '/* Korean */
Public Const HPDF_LANG_KS = "ks"       '/* Kashmiri */
Public Const HPDF_LANG_KU = "ku"       '/* Kurdish */
Public Const HPDF_LANG_KY = "ky"       '/* Kirghiz */
Public Const HPDF_LANG_LA = "la"       '/* Latin */
Public Const HPDF_LANG_LN = "ln"       '/* Lingala */
Public Const HPDF_LANG_LO = "lo"       '/* Laothian */
Public Const HPDF_LANG_LT = "lt"       '/* Lithuanian */
Public Const HPDF_LANG_LV = "lv"       '/* Latvian,Lettish */
Public Const HPDF_LANG_MG = "mg"       '/* Malagasy */
Public Const HPDF_LANG_MI = "mi"       '/* Maori */
Public Const HPDF_LANG_MK = "mk"       '/* Macedonian */
Public Const HPDF_LANG_ML = "ml"       '/* Malayalam */
Public Const HPDF_LANG_MN = "mn"       '/* Mongolian */
Public Const HPDF_LANG_MO = "mo"       '/* Moldavian */
Public Const HPDF_LANG_MR = "mr"       '/* Marathi */
Public Const HPDF_LANG_MS = "ms"       '/* Malay */
Public Const HPDF_LANG_MT = "mt"       '/* Maltese */
Public Const HPDF_LANG_MY = "my"       '/* Burmese */
Public Const HPDF_LANG_NA = "na"       '/* Nauru */
Public Const HPDF_LANG_NE = "ne"       '/* Nepali */
Public Const HPDF_LANG_NL = "nl"       '/* Dutch */
Public Const HPDF_LANG_NO = "no"       '/* Norwegian */
Public Const HPDF_LANG_OC = "oc"       '/* Occitan */
Public Const HPDF_LANG_OM = "om"       '/* (Afan)Oromo */
Public Const HPDF_LANG_OR = "or"       '/* Oriya */
Public Const HPDF_LANG_PA = "pa"       '/* Punjabi */
Public Const HPDF_LANG_PL = "pl"       '/* Polish */
Public Const HPDF_LANG_PS = "ps"       '/* Pashto,Pushto */
Public Const HPDF_LANG_PT = "pt"       '/* Portuguese  */
Public Const HPDF_LANG_QU = "qu"       '/* Quechua */
Public Const HPDF_LANG_RM = "rm"       '/* Rhaeto-Romance */
Public Const HPDF_LANG_RN = "rn"       '/* Kirundi */
Public Const HPDF_LANG_RO = "ro"       '/* Romanian */
Public Const HPDF_LANG_RU = "ru"       '/* Russian */
Public Const HPDF_LANG_RW = "rw"       '/* Kinyarwanda */
Public Const HPDF_LANG_SA = "sa"       '/* Sanskrit */
Public Const HPDF_LANG_SD = "sd"       '/* Sindhi */
Public Const HPDF_LANG_SG = "sg"       '/* Sangro */
Public Const HPDF_LANG_SH = "sh"       '/* Serbo-Croatian */
Public Const HPDF_LANG_SI = "si"       '/* Singhalese */
Public Const HPDF_LANG_SK = "sk"       '/* Slovak */
Public Const HPDF_LANG_SL = "sl"       '/* Slovenian */
Public Const HPDF_LANG_SM = "sm"       '/* Samoan */
Public Const HPDF_LANG_SN = "sn"       '/* Shona */
Public Const HPDF_LANG_SO = "so"       '/* Somali */
Public Const HPDF_LANG_SQ = "sq"       '/* Albanian */
Public Const HPDF_LANG_SR = "sr"       '/* Serbian */
Public Const HPDF_LANG_SS = "ss"       '/* Siswati */
Public Const HPDF_LANG_ST = "st"       '/* Sesotho */
Public Const HPDF_LANG_SU = "su"       '/* Sundanese */
Public Const HPDF_LANG_SV = "sv"       '/* Swedish */
Public Const HPDF_LANG_SW = "sw"       '/* Swahili */
Public Const HPDF_LANG_TA = "ta"       '/* Tamil */
Public Const HPDF_LANG_TE = "te"       '/* Tegulu */
Public Const HPDF_LANG_TG = "tg"       '/* Tajik */
Public Const HPDF_LANG_TH = "th"       '/* Thai */
Public Const HPDF_LANG_TI = "ti"       '/* Tigrinya */
Public Const HPDF_LANG_TK = "tk"       '/* Turkmen */
Public Const HPDF_LANG_TL = "tl"       '/* Tagalog */
Public Const HPDF_LANG_TN = "tn"       '/* Setswanato Tonga */
Public Const HPDF_LANG_TR = "tr"       '/* Turkish */
Public Const HPDF_LANG_TS = "ts"       '/* Tsonga */
Public Const HPDF_LANG_TT = "tt"       '/* Tatar */
Public Const HPDF_LANG_TW = "tw"       '/* Twi */
Public Const HPDF_LANG_UK = "uk"       '/* Ukrainian */
Public Const HPDF_LANG_UR = "ur"       '/* Urdu */
Public Const HPDF_LANG_UZ = "uz"       '/* Uzbek */
Public Const HPDF_LANG_VI = "vi"       '/* Vietnamese */
Public Const HPDF_LANG_VO = "vo"       '/* Volapuk */
Public Const HPDF_LANG_WO = "wo"       '/* Wolof */
Public Const HPDF_LANG_XH = "xh"       '/* Xhosa */
Public Const HPDF_LANG_YO = "yo"       '/* Yoruba */
Public Const HPDF_LANG_ZH = "zh"       '/* Chinese */
Public Const HPDF_LANG_ZU = "zu"       '/* Zulu */


'/*----------------------------------------------------------------------------*/
'/*----- Graphics mode --------------------------------------------------------*/

Public Const HPDF_GMODE_PAGE_DESCRIPTION = &H1
Public Const HPDF_GMODE_PATH_OBJECT = &H2
Public Const HPDF_GMODE_TEXT_OBJECT = &H4
Public Const HPDF_GMODE_CLIPPING_PATH = &H8
Public Const HPDF_GMODE_SHADING = &H10
Public Const HPDF_GMODE_INLINE_IMAGE = &H20
Public Const HPDF_GMODE_EXTERNAL_OBJECT = &H40


'/*----------------------------------------------------------------------------*/

'#endif '/* _HPDF_CONSTS_H */
