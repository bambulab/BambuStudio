VERSION 5.00
Begin VB.Form Form1 
   Caption         =   "Form1"
   ClientHeight    =   3090
   ClientLeft      =   60
   ClientTop       =   450
   ClientWidth     =   4680
   LinkTopic       =   "Form1"
   ScaleHeight     =   3090
   ScaleWidth      =   4680
   StartUpPosition =   3  '系統預設值
   Begin VB.CommandButton Command1 
      Caption         =   "Command1"
      Height          =   1185
      Left            =   960
      TabIndex        =   0
      Top             =   870
      Width           =   2595
   End
End
Attribute VB_Name = "Form1"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False

Private Sub Command1_Click()
    Dim page_title  As String

    Dim pdf         As Long
    Dim font        As Long
    Dim page        As Long
    Dim fname       As String * 256

    Dim samp_text   As String
    Dim samp_text2  As String
    Dim tw          As Single
    Dim fsize       As Single
    Dim i           As Long
    Dim length      As Long

    Dim angle1      As Single
    Dim angle2      As Single
    Dim rad1        As Single
    Dim rad2        As Single
    Dim r           As Single
    Dim g           As Single
    Dim b           As Single

    Dim ypos        As Single
    
    page_title = "Text Demo"
    samp_text = "abcdefgABCDEFG123!#$%&+-@?"
    samp_text2 = "The quick brown fox jumps over the lazy dog."

    
    fname = App.Path & IIf(Right(App.Path, 1) = "\", "", "\") & "text_demo.pdf"
    
    pdf = HPDF_New(AddressOf error_handler, ByVal vbNullString)
    
    If pdf = 0 Then
        'printf ("error: cannot create PdfDoc object\n")
        Exit Sub
    End If

    '/* set compression mode */
    Call HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL)

    '/* create default-font */
    sTemp1 = "Helvetica" '& Chr(0)
    font = HPDF_GetFont(pdf, ByVal sTemp1, ByVal vbNullString)
    'Dim stemp   As string
    'stemp = HPDF_LoadTTFontFromFile(pdf, ByVal "c:\windows\fonts\arial.ttf", HPDF_TRUE)
    
    '/* add a new page object. */
    page = HPDF_AddPage(pdf)

    '/* draw grid to the page */
    'Call print_grid(pdf, page)

    '/* print the lines of the page.
    Call HPDF_Page_SetLineWidth(page, 1)
    Call HPDF_Page_Rectangle(page, 50, 50, HPDF_Page_GetWidth(page) - 100, HPDF_Page_GetHeight(page) - 110)
    Call HPDF_Page_Stroke(page)
    
    '/* print the title of the page (with positioning center). */
    Call HPDF_Page_SetFontAndSize(page, font, 24)
    tw = HPDF_Page_TextWidth(page, ByVal page_title)
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, (HPDF_Page_GetWidth(page) - tw) / 2, HPDF_Page_GetHeight(page) - 50, ByVal page_title)
    Call HPDF_Page_EndText(page)

    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_MoveTextPos(page, 60, HPDF_Page_GetHeight(page) - 60)

    '/*
    ' * font size
    ' */
    Dim buf     As String * 50
    fsize = 8
    Do While (fsize < 60)

        '/* set style and size of font. */
        Call HPDF_Page_SetFontAndSize(page, font, fsize)

        '/* set the position of the text. */
        Call HPDF_Page_MoveTextPos(page, 0, -5 - fsize)

        '/* measure the number of characters which included in the page. */
        'strcpy(buf, samp_text)
        samp_text = buf
        length = HPDF_Page_MeasureText(page, samp_text, HPDF_Page_GetWidth(page) - 120, HPDF_FALSE, 0&)

        '/* truncate the text. */
        'buf(length) = &H0

        Call HPDF_Page_ShowText(page, buf)

        '/* print the description. */
        Call HPDF_Page_MoveTextPos(page, 0, -10)
        Call HPDF_Page_SetFontAndSize(page, font, 8)
        buf = "Fontsize=" & Format(fsize, "0.0")
        Call HPDF_Page_ShowText(page, buf)

        fsize = fsize * 1.5
    Loop

    '/*
    ' * font color
    ' */
    Call HPDF_Page_SetFontAndSize(page, font, 8)
    Call HPDF_Page_MoveTextPos(page, 0, -30)
    Call HPDF_Page_ShowText(page, "Font color")

    Call HPDF_Page_SetFontAndSize(page, font, 18)
    Call HPDF_Page_MoveTextPos(page, 0, -20)
    length = Len(samp_text)
    
    Dim buf1 As String * 2
    For i = 0 To length
        r = i / length
        g = 1 - (i / length)
        CopyMemory ByVal buf1, VarPtr(samp_text) + i, 1
        'buf[1] = 0x00

        Call HPDF_Page_SetRGBFill(page, r, g, 0#)
        Call HPDF_Page_ShowText(page, buf1)
    Next i
    Call HPDF_Page_MoveTextPos(page, 0, -25)

    For i = 0 To length
        r = i / length
        b = 1 - (i / length)
        CopyMemory ByVal buf1, VarPtr(samp_text) + i, 1
        'buf[1] = 0x00

        Call HPDF_Page_SetRGBFill(page, r, 0#, b)
        Call HPDF_Page_ShowText(page, buf1)
    Next i
    Call HPDF_Page_MoveTextPos(page, 0, -25)

    For i = 0 To length
        b = i / length
        g = 1 - (i / length)
        CopyMemory ByVal buf1, VarPtr(samp_text) + i, 1
        'buf[1] = 0x00

        Call HPDF_Page_SetRGBFill(page, 0#, g, b)
        Call HPDF_Page_ShowText(page, buf)
    Next i

    HPDF_Page_EndText (page)

    ypos = 450

    '/*
    ' * Font rendering mode
    ' */
    Call HPDF_Page_SetFontAndSize(page, font, 32)
    Call HPDF_Page_SetRGBFill(page, 0.5, 0.5, 0#)
    Call HPDF_Page_SetLineWidth(page, 1.5)

     '/* PDF_FILL */
    Call show_description(page, 60, ypos, "RenderingMode=PDF_FILL")
    Call HPDF_Page_SetTextRenderingMode(page, HPDF_FILL)
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, 60, ypos, "ABCabc123")
    Call HPDF_Page_EndText(page)

    '/* PDF_STROKE */
    Call show_description(page, 60, ypos - 50, "RenderingMode=PDF_STROKE")
    Call HPDF_Page_SetTextRenderingMode(page, HPDF_STROKE)
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, 60, ypos - 50, "ABCabc123")
    Call HPDF_Page_EndText(page)

    '/* PDF_FILL_THEN_STROKE */
    Call show_description(page, 60, ypos - 100, "RenderingMode=PDF_FILL_THEN_STROKE")
    Call HPDF_Page_SetTextRenderingMode(page, HPDF_FILL_THEN_STROKE)
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, 60, ypos - 100, "ABCabc123")
    Call HPDF_Page_EndText(page)

    '/* PDF_FILL_CLIPPING */
    Call show_description(page, 60, ypos - 150, "RenderingMode=PDF_FILL_CLIPPING")
    Call HPDF_Page_GSave(page)
    Call HPDF_Page_SetTextRenderingMode(page, HPDF_FILL_CLIPPING)
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, 60, ypos - 150, "ABCabc123")
    Call HPDF_Page_EndText(page)
    Call show_stripe_pattern(page, 60, ypos - 150)
    Call HPDF_Page_GRestore(page)

    '/* PDF_STROKE_CLIPPING */
    Call show_description(page, 60, ypos - 200, "RenderingMode=PDF_STROKE_CLIPPING")
    Call HPDF_Page_GSave(page)
    Call HPDF_Page_SetTextRenderingMode(page, HPDF_STROKE_CLIPPING)
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, 60, ypos - 200, "ABCabc123")
    Call HPDF_Page_EndText(page)
    Call show_stripe_pattern(page, 60, ypos - 200)
    Call HPDF_Page_GRestore(page)

    '/* PDF_FILL_STROKE_CLIPPING */
    Call show_description(page, 60, ypos - 250, "RenderingMode=PDF_FILL_STROKE_CLIPPING")
    Call HPDF_Page_GSave(page)
    Call HPDF_Page_SetTextRenderingMode(page, HPDF_FILL_STROKE_CLIPPING)
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, 60, ypos - 250, "ABCabc123")
    Call HPDF_Page_EndText(page)
    Call show_stripe_pattern(page, 60, ypos - 250)
    Call HPDF_Page_GRestore(page)

    '/* Reset text attributes */
    Call HPDF_Page_SetTextRenderingMode(page, HPDF_FILL)
    Call HPDF_Page_SetRGBFill(page, 0, 0, 0)
    Call HPDF_Page_SetFontAndSize(page, font, 30)


    '/*
    ' * Rotating text
    ' */
    angle1 = 30                   '/* A rotation of 30 degrees. */
    rad1 = angle1 / 180 * 3.141592 '/* Calculate the radian value. */

    Call show_description(page, 320, ypos - 60, "Rotating text")
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_SetTextMatrix(page, Cos(rad1), Sin(rad1), -Sin(rad1), Cos(rad1), 330, ypos - 60)
    Call HPDF_Page_ShowText(page, "ABCabc123")
    Call HPDF_Page_EndText(page)


    '/*
    ' * Skewing text.
    ' */
    Call show_description(page, 320, ypos - 120, "Skewing text")
    Call HPDF_Page_BeginText(page)

    angle1 = 10
    angle2 = 20
    rad1 = angle1 / 180 * 3.141592
    rad2 = angle2 / 180 * 3.141592

    Call HPDF_Page_SetTextMatrix(page, 1, Tan(rad1), Tan(rad2), 1, 320, ypos - 120)
    Call HPDF_Page_ShowText(page, "ABCabc123")
    Call HPDF_Page_EndText(page)


    '/*
    ' * scaling text (X direction)
    ' */
    Call show_description(page, 320, ypos - 175, "Scaling text (X direction)")
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_SetTextMatrix(page, 1.5, 0, 0, 1, 320, ypos - 175)
    Call HPDF_Page_ShowText(page, "ABCabc12")
    Call HPDF_Page_EndText(page)


    '/*
    ' * scaling text (Y direction)
    ' */
    Call show_description(page, 320, ypos - 250, "Scaling text (Y direction)")
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_SetTextMatrix(page, 1, 0, 0, 2, 320, ypos - 250)
    Call HPDF_Page_ShowText(page, "ABCabc123")
    Call HPDF_Page_EndText(page)


    '/*
    ' * char spacing, word spacing
    ' */

    Call show_description(page, 60, 140, "char-spacing 0")
    Call show_description(page, 60, 100, "char-spacing 1.5")
    Call show_description(page, 60, 60, "char-spacing 1.5, word-spacing 2.5")

    Call HPDF_Page_SetFontAndSize(page, font, 20)
    Call HPDF_Page_SetRGBFill(page, 0.1, 0.3, 0.1)

    '/* char-spacing 0 */
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, 60, 140, ByVal samp_text2)
    Call HPDF_Page_EndText(page)

    '/* char-spacing 1.5 */
    Call HPDF_Page_SetCharSpace(page, 1.5)

    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, 60, 100, ByVal samp_text2)
    Call HPDF_Page_EndText(page)

    '/* char-spacing 1.5, word-spacing 3.5 */
    Call HPDF_Page_SetWordSpace(page, 2.5)

    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_TextOut(page, 60, 60, ByVal samp_text2)
    Call HPDF_Page_EndText(page)

    '/* save the document to a file */
    Call HPDF_SaveToFile(pdf, ByVal fname)

    '/* clean up */
    Call HPDF_Free(pdf)

End Sub

Public Function show_description(page As Long, x As Single, y As Single, ByVal text As String) As Long
    Dim fsize           As Single
    Dim font            As Long
    Dim c               As HPDF_RGBColor
    fsize = HPDF_Page_GetCurrentFontSize(page)
    font = HPDF_Page_GetCurrentFont(page)
    c = HPDF_Page_GetRGBFill(page)
    Call HPDF_Page_BeginText(page)
    Call HPDF_Page_SetRGBFill(page, 0, 0, 0)
    Call HPDF_Page_SetTextRenderingMode(page, HPDF_FILL)
    Call HPDF_Page_SetFontAndSize(page, font, 10)
    Call HPDF_Page_TextOut(page, x, y - 12, ByVal text)
    Call HPDF_Page_EndText(page)
    Call HPDF_Page_SetFontAndSize(page, font, fsize)
    Call HPDF_Page_SetRGBFill(page, c.r, c.g, c.b)
End Function

Public Function show_stripe_pattern(ByVal page As Long, ByVal x As Single, ByVal y As Single)
    Dim iy As Long
    Do While (iy < 50)
        Call HPDF_Page_SetRGBStroke(page, 0#, 0#, 0.5)
        Call HPDF_Page_SetLineWidth(page, 1)
        Call HPDF_Page_MoveTo(page, x, y + iy)
        Call HPDF_Page_LineTo(page, x + HPDF_Page_TextWidth(page, "ABCabc123"), y + iy)
        Call HPDF_Page_Stroke(page)
        iy = iy + 3
    Loop
    Call HPDF_Page_SetLineWidth(page, 2.5)
End Function
