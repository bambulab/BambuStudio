<*+CHANGESYM*>
<*+M2EXTENSIONS*>
<*+O2EXTENSIONS*>
<*+MAIN*>
MODULE test;

IMPORT
  Out,
  SYSTEM,
  w := Windows,
  h := hpdf;

PROCEDURE ["StdCall"] ErrorProc(error_no: h.HPDF_STATUS; detail_no: h.HPDF_STATUS; <*+WOFF301*>user_data: w.PVOID<*-WOFF301*>);
BEGIN
  Out.String("ERROR: ");
  Out.Int(error_no,  1);
  Out.String(" - ");
  Out.Int(detail_no, 1);
  Out.Ln;
  HALT(1);
END ErrorProc;

PROCEDURE draw_line (page: h.HPDF_Page; x: h.HPDF_REAL; y: h.HPDF_REAL; text: h.HPDF_PCHAR);
BEGIN
  h.HPDF_Page_BeginText(page);
  h.HPDF_Page_MoveTextPos(page, x, y - 10);
  h.HPDF_Page_ShowText(page, text);
  h.HPDF_Page_EndText(page);

  h.HPDF_Page_MoveTo(page, x, y - 15);
  h.HPDF_Page_LineTo(page, x + 220, y - 15);
  h.HPDF_Page_Stroke(page);
END draw_line;

PROCEDURE draw_line2 (page: h.HPDF_Page; x: h.HPDF_REAL; y: h.HPDF_REAL; text: h.HPDF_PCHAR);
BEGIN
  h.HPDF_Page_BeginText(page);
  h.HPDF_Page_MoveTextPos(page, x, y);
  h.HPDF_Page_ShowText(page, text);
  h.HPDF_Page_EndText(page);

  h.HPDF_Page_MoveTo(page, x + 30, y - 25);
  h.HPDF_Page_LineTo(page, x + 160, y - 25);
  h.HPDF_Page_Stroke(page);
END draw_line2;

PROCEDURE draw_rect (page: h.HPDF_Page; x: h.HPDF_REAL; y: h.HPDF_REAL; text: h.HPDF_PCHAR);
BEGIN
  h.HPDF_Page_BeginText(page);
  h.HPDF_Page_MoveTextPos(page, x, y - 10);
  h.HPDF_Page_ShowText(page, text);
  h.HPDF_Page_EndText(page);

  h.HPDF_Page_Rectangle(page, x, y - 40, 220, 25);
--  h.HPDF_Page_Rectangle(page, x, y - 40, 100, 100);
END draw_rect;


VAR
  pdf:  h.HPDF_Doc;
  font: h.HPDF_Font;
  page: h.HPDF_Page;

  x, y,
  x0, y0,
  x1, y1,
  x2, y2,
  x3, y3,
  tw: h.HPDF_REAL;
  ffilename: h.HPDF_PCHAR;
CONST
  page_title = 'Демонстрация штрихов';
  fname      = 'LineDemo.pdf';

  DASH_MODE1 = ARRAY OF h.HPDF_UINT16          {3};
  DASH_MODE2 = ARRAY OF h.HPDF_UINT16       {7, 3};
  DASH_MODE3 = ARRAY OF h.HPDF_UINT16 {8, 7, 2, 7};

BEGIN
  pdf := h.HPDF_New(ErrorProc, NIL);

  IF pdf = NIL THEN
    Out.String("error: cannot create PdfDoc object"); Out.Ln();
    HALT(1);
  END;


    h.HPDF_SetCompressionMode(pdf, h.HPDF_COMP_ALL);

    ffilename := h.HPDF_LoadTTFontFromFile(pdf, w.GetPSTR('PTS55F.ttf'), TRUE);
    (* create default-font *)
    font := h.HPDF_GetFont(pdf, ffilename, w.GetPSTR('CP1251'));
    (* add a new page object. *)
    page := h.HPDF_AddPage(pdf);

    (* print the lines of the page. *)
    h.HPDF_Page_SetLineWidth(page, 1);
    x := h.HPDF_Page_GetWidth(page) - 100;
    y := h.HPDF_Page_GetHeight(page) - 110;
    h.HPDF_Page_Rectangle(page, 50, 50, x, y);
    h.HPDF_Page_Stroke(page);

    (* print the title of the page (with positioning center). *)
    h.HPDF_Page_SetFontAndSize(page, font, 24);
    tw := h.HPDF_Page_TextWidth(page, w.GetPSTR(page_title));
    h.HPDF_Page_BeginText(page);

    x := (h.HPDF_Page_GetWidth(page) - tw) / 2;
    y := h.HPDF_Page_GetHeight(page) - 50;
    h.HPDF_Page_MoveTextPos (page, x, y);

    h.HPDF_Page_ShowText (page, w.GetPSTR(page_title));
    h.HPDF_Page_EndText (page);

    h.HPDF_Page_SetFontAndSize (page, font, 10);

    (* Draw various widths of lines. *)
    h.HPDF_Page_SetLineWidth (page, 0);
    draw_line (page, 60, 770, w.GetPSTR('толщина линии := 0'));

    h.HPDF_Page_SetLineWidth (page, 1.0);
    draw_line (page, 60, 740, w.GetPSTR('толщина линии := 1.0'));

    h.HPDF_Page_SetLineWidth (page, 2.0);
    draw_line (page, 60, 710, w.GetPSTR('толщина линии := 2.0'));

    (* Line dash pattern *)
    h.HPDF_Page_SetLineWidth (page, 1.0);

    h.HPDF_Page_SetDash (page, SYSTEM.REF(DASH_MODE1), 1, 1);
    draw_line (page, 60, 680, w.GetPSTR('dash_ptn=[3], фазы=1 -- 2 вкл, 3 выкл, 3 вкл...'));

    h.HPDF_Page_SetDash (page, SYSTEM.REF(DASH_MODE2), 2, 2);
    draw_line (page, 60, 650, w.GetPSTR('dash_ptn=[7, 3], phase=2 -- 5 вкл 3 выкл, 7 вкл,...'));

    h.HPDF_Page_SetDash (page, SYSTEM.REF(DASH_MODE3), 4, 0);
    draw_line (page, 60, 620, w.GetPSTR('dash_ptn=[8, 7, 2, 7], фазы=0'));

    h.HPDF_Page_SetDash (page, NIL, 0, 0);

    h.HPDF_Page_SetLineWidth (page, 30);
    h.HPDF_Page_SetRGBStroke (page, 0.0, 0.5, 0.0);

    (* Line Cap Style *)
    h.HPDF_Page_SetLineCap (page, h.HPDF_BUTT_END);
    draw_line2 (page, 60, 570, w.GetPSTR('PDF_BUTT_END'));

    h.HPDF_Page_SetLineCap (page, h.HPDF_ROUND_END);
    draw_line2 (page, 60, 505, w.GetPSTR('PDF_ROUND_END'));

    h.HPDF_Page_SetLineCap (page, h.HPDF_PROJECTING_SQUARE_END);
    draw_line2 (page, 60, 440, w.GetPSTR('PDF_PROJECTING_SQUARE_END'));

    (* Line Join Style *)
    h.HPDF_Page_SetLineWidth (page, 30);
    h.HPDF_Page_SetRGBStroke (page, 0.0, 0.0, 0.5);

    h.HPDF_Page_SetLineJoin (page, h.HPDF_MITER_JOIN);
    h.HPDF_Page_MoveTo (page, 120, 300);
    h.HPDF_Page_LineTo (page, 160, 340);
    h.HPDF_Page_LineTo (page, 200, 300);
    h.HPDF_Page_Stroke (page);

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, 60, 360);
    h.HPDF_Page_ShowText (page, w.GetPSTR('PDF_MITER_JOIN'));
    h.HPDF_Page_EndText (page);

    h.HPDF_Page_SetLineJoin (page, h.HPDF_ROUND_JOIN);
    h.HPDF_Page_MoveTo (page, 120, 195);
    h.HPDF_Page_LineTo (page, 160, 235);
    h.HPDF_Page_LineTo (page, 200, 195);
    h.HPDF_Page_Stroke (page);

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, 60, 255);
    h.HPDF_Page_ShowText (page, w.GetPSTR('PDF_ROUND_JOIN'));
    h.HPDF_Page_EndText (page);

    h.HPDF_Page_SetLineJoin (page, h.HPDF_BEVEL_JOIN);
    h.HPDF_Page_MoveTo (page, 120, 90);
    h.HPDF_Page_LineTo (page, 160, 130);
    h.HPDF_Page_LineTo (page, 200, 90);
    h.HPDF_Page_Stroke (page);

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, 60, 150);
    h.HPDF_Page_ShowText (page, w.GetPSTR('PDF_BEVEL_JOIN'));
    h.HPDF_Page_EndText (page);

    (* Draw Rectangle *)
    h.HPDF_Page_SetLineWidth (page, 2);
    h.HPDF_Page_SetRGBStroke (page, 0, 0, 0);
    h.HPDF_Page_SetRGBFill (page, 0.75, 0.0, 0.0);

    draw_rect (page, 300, 770, w.GetPSTR('Штрих'));
    h.HPDF_Page_Stroke (page);

    draw_rect (page, 300, 720, w.GetPSTR('Заполнение'));
    h.HPDF_Page_Fill (page);

    draw_rect (page, 300, 670, w.GetPSTR('Заполнение, потом штрих'));
    h.HPDF_Page_FillStroke (page);

    (* Clip Rect *)
    h.HPDF_Page_GSave (page);  (* Save the current graphic state *)
    draw_rect (page, 300, 620, w.GetPSTR('Прямоугольник отсечения'));
    h.HPDF_Page_Clip (page);
    h.HPDF_Page_Stroke (page);
    h.HPDF_Page_SetFontAndSize (page, font, 13);

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, 290, 600);
    h.HPDF_Page_SetTextLeading (page, 12);
    h.HPDF_Page_ShowText         (page, w.GetPSTR('Отсечение Отсечение Отсечение Отсечение'));
    h.HPDF_Page_ShowTextNextLine (page, w.GetPSTR('Отсечение Отсечение Отсечение Отсечение'));
    h.HPDF_Page_ShowTextNextLine (page, w.GetPSTR('Отсечение Отсечение Отсечение Отсечение'));
    h.HPDF_Page_EndText (page);
    h.HPDF_Page_GRestore (page);

    x0 := 330;
    y0 := 440;
    x1 := 430;
    y1 := 530;
    x2 := 480;
    y2 := 470;
    x3 := 480;
    y3 := 90;

    (* Curve Example(CurveTo2) *)
    h.HPDF_Page_SetRGBFill (page, 0, 0, 0);

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, 300, 540);
    h.HPDF_Page_ShowText (page, w.GetPSTR('CurveTo2(x1, y1, x2. y2)'));
    h.HPDF_Page_EndText (page);

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, x0 + 5, y0 - 5);
    h.HPDF_Page_ShowText (page, w.GetPSTR('Текущая точка'));
    h.HPDF_Page_MoveTextPos (page, x1 - x0, y1 - y0);
    h.HPDF_Page_ShowText (page, w.GetPSTR('(x1, y1)'));
    h.HPDF_Page_MoveTextPos (page, x2 - x1, y2 - y1);
    h.HPDF_Page_ShowText (page, w.GetPSTR('(x2, y2)'));
    h.HPDF_Page_EndText (page);

    h.HPDF_Page_SetDash (page, NIL, 0, 0);

    h.HPDF_Page_SetLineWidth (page, 0.5);
    h.HPDF_Page_MoveTo (page, x1, y1);
    h.HPDF_Page_LineTo (page, x2, y2);
    h.HPDF_Page_Stroke (page);

    h.HPDF_Page_SetDash (page, NIL, 0, 0);

    h.HPDF_Page_SetLineWidth (page, 1.5);

    h.HPDF_Page_MoveTo (page, x0, y0);
    h.HPDF_Page_CurveTo2 (page, x1, y1, x2, y2);
    h.HPDF_Page_Stroke (page);

    (* Curve Example(CurveTo3) *)
    y0 := y0 - 150;
    y1 := y1 - 150;
    y2 := y2 - 150;

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, 300, 390);
    h.HPDF_Page_ShowText (page, w.GetPSTR('CurveTo3(x1, y1, x2. y2)'));
    h.HPDF_Page_EndText (page);

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, x0 + 5, y0 - 5);
    h.HPDF_Page_ShowText (page, w.GetPSTR('Текущая точка'));
    h.HPDF_Page_MoveTextPos (page, x1 - x0, y1 - y0);
    h.HPDF_Page_ShowText (page, w.GetPSTR('(x1, y1)'));
    h.HPDF_Page_MoveTextPos (page, x2 - x1, y2 - y1);
    h.HPDF_Page_ShowText (page, w.GetPSTR('(x2, y2)'));
    h.HPDF_Page_EndText (page);

    h.HPDF_Page_SetDash (page, SYSTEM.REF(DASH_MODE1), 1, 1);

    h.HPDF_Page_SetLineWidth (page, 0.5);
    h.HPDF_Page_MoveTo (page, x0, y0);
    h.HPDF_Page_LineTo (page, x1, y1);
    h.HPDF_Page_Stroke (page);

    h.HPDF_Page_SetDash (page, NIL, 0, 0);

    h.HPDF_Page_SetLineWidth (page, 1.5);
    h.HPDF_Page_MoveTo (page, x0, y0);
    h.HPDF_Page_CurveTo3 (page, x1, y1, x2, y2);
    h.HPDF_Page_Stroke (page);

    (* Curve Example(CurveTo) *)
    y0 := y0 - 150;
    y1 := y1 - 160;
    y2 := y2 - 130;
    x2 := x2 + 10;

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, 300, 240);
    h.HPDF_Page_ShowText (page, w.GetPSTR('CurveTo(x1, y1, x2. y2, x3, y3)'));
    h.HPDF_Page_EndText (page);

    h.HPDF_Page_BeginText (page);
    h.HPDF_Page_MoveTextPos (page, x0 + 5, y0 - 5);
    h.HPDF_Page_ShowText (page, w.GetPSTR('Текущая точка'));
    h.HPDF_Page_MoveTextPos (page, x1 - x0, y1 - y0);
    h.HPDF_Page_ShowText (page, w.GetPSTR('(x1, y1)'));
    h.HPDF_Page_MoveTextPos (page, x2 - x1, y2 - y1);
    h.HPDF_Page_ShowText (page, w.GetPSTR('(x2, y2)'));
    h.HPDF_Page_MoveTextPos (page, x3 - x2, y3 - y2);
    h.HPDF_Page_ShowText (page, '(x3, y3)');
    h.HPDF_Page_EndText (page);

    h.HPDF_Page_SetDash (page, SYSTEM.REF(DASH_MODE1), 1, 1);

    h.HPDF_Page_SetLineWidth (page, 0.5);
    h.HPDF_Page_MoveTo (page, x0, y0);
    h.HPDF_Page_LineTo (page, x1, y1);
    h.HPDF_Page_Stroke (page);
    h.HPDF_Page_MoveTo (page, x2, y2);
    h.HPDF_Page_LineTo (page, x3, y3);
    h.HPDF_Page_Stroke (page);

    h.HPDF_Page_SetDash (page, NIL, 0, 0);

    h.HPDF_Page_SetLineWidth (page, 1.5);
    h.HPDF_Page_MoveTo (page, x0, y0);
    h.HPDF_Page_CurveTo (page, x1, y1, x2, y2, x3, y3);
    h.HPDF_Page_Stroke (page);

    (* save the document to a file *)
    h.HPDF_SaveToFile (pdf, w.GetPSTR(fname));

  
END test.
