//
//  TutkPlayer.m
//  BambuStudio
//
//  Created by cmguo on 2021/12/7.
//

#import "wxMediaCtrl2.h"
#import "wx/mediactrl.h"
#include <boost/log/trivial.hpp>

#import <Foundation/Foundation.h>
#import <TutkPlayer/TutkPlayer.h>

static void tutk_log(void const * ctx, int level, char const * msg)
{
    if (level == 1) {
        wxString msg2(msg);
        if (msg2.EndsWith("]")) {
            int n = msg2.find_last_of('[');
            if (n != wxString::npos) {
                long val = 0;
                int * error = (int *) ctx;
                if (msg2.SubString(n + 1, msg2.Length() - 2).ToLong(&val))
                    *error = (int) val;
            }
        }
    }
    BOOST_LOG_TRIVIAL(info) << msg;
}

wxMediaCtrl2::wxMediaCtrl2(wxWindow * parent, wxSize const & size)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, size)
{
    NSView * imageView = (NSView *) GetHandle();
    imageView.layer = [[CALayer alloc] init];
    CGColorRef color = CGColorCreateGenericRGB(0, 0, 0, 1.0f);
    imageView.layer.backgroundColor = color;
    CGColorRelease(color);
    imageView.wantsLayer = YES;
    TutkPlayer * player = [[TutkPlayer alloc] initWithImageView: imageView];
    [player setLogger: tutk_log withContext: &m_error];
    m_player = player;
    m_state = wxMEDIASTATE_STOPPED;
}

void wxMediaCtrl2::Load(wxURI url)
{
    TutkPlayer * player = (TutkPlayer *) m_player;
    [player close];
    [player open: url.BuildURI().Mid(8).ToUTF8()];
    m_error = 0;
    wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
    wxPostEvent(this, event);
}

void wxMediaCtrl2::Play()
{
    TutkPlayer * player2 = (TutkPlayer *) m_player;
    [player2 play];
    if (m_state != wxMEDIASTATE_PLAYING) {
        m_state = wxMEDIASTATE_PLAYING;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        wxPostEvent(this, event);
    }
}

void wxMediaCtrl2::Stop()
{
    TutkPlayer * player2 = (TutkPlayer *) m_player;
    [player2 stop];
    if (m_state != wxMEDIASTATE_STOPPED) {
        m_state = wxMEDIASTATE_STOPPED;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        wxPostEvent(this, event);
    }
}

wxMediaState wxMediaCtrl2::GetState() const
{
    return m_state;
}

wxSize wxMediaCtrl2::DoGetBestSize() const
{
    TutkPlayer * player2 = (TutkPlayer *) m_player;
    NSSize size = [player2 videoSize];
    return {(int) size.width, (int) size.height};
}


/* textColor for NSButton */

@implementation NSButton (NSButton_Extended)

- (NSColor *)textColor
{
    NSAttributedString *attrTitle = [self attributedTitle];
    int len = [attrTitle length];
    NSRange range = NSMakeRange(0, MIN(len, 1)); // get the font attributes from the first character
    NSDictionary *attrs = [attrTitle fontAttributesInRange:range];
    NSColor *textColor = [NSColor controlTextColor];
    if (attrs)
    {
        textColor = [attrs objectForKey:NSForegroundColorAttributeName];
    }
    
    return textColor;
}

- (void)setTextColor:(NSColor *)textColor
{
    NSMutableAttributedString *attrTitle =
        [[NSMutableAttributedString alloc] initWithAttributedString:[self attributedTitle]];
    int len = [attrTitle length];
    NSRange range = NSMakeRange(0, len);
    [attrTitle addAttribute:NSForegroundColorAttributeName value:textColor range:range];
    [attrTitle fixAttributesInRange:range];
    [self setAttributedTitle:attrTitle];
    [attrTitle release];
}

@end

