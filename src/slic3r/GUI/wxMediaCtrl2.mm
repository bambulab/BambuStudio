//
//  TutkPlayer.m
//  BambuSlicer
//
//  Created by cmguo on 2021/12/7.
//

#import "wxMediaCtrl2.h"
#import "wx/mediactrl.h"
#include <boost/log/trivial.hpp>

#import <Foundation/Foundation.h>
#import <TutkPlayer/TutkPlayer.h>

static void tutk_log(void const *, int level, char const * msg)
{
    BOOST_LOG_TRIVIAL(info) << msg;
}

wxMediaCtrl2::wxMediaCtrl2(wxWindow * parent)
    : wxWindow(parent, wxID_ANY)
{
    NSView * imageView = (NSView *) GetHandle();
    imageView.layer = [[CALayer alloc] init];
    imageView.wantsLayer = YES;
    TutkPlayer * player = [[TutkPlayer alloc] initWithImageView: imageView];
    [player setLogger: tutk_log withContext: this];
    m_player = player;
    m_state = wxMEDIASTATE_STOPPED;
}

void wxMediaCtrl2::Load(wxURI url)
{
    TutkPlayer * player = (TutkPlayer *) m_player;
    [player close];
    [player open: url.BuildURI().Mid(8).ToUTF8()];
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
