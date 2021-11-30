//
//  TutkPlayer.m
//  PrusaSlicer
//
//  Created by cmguo on 2021/12/7.
//

#import "wxMediaCtrl2.h"
#import "wx/mediactrl.h"

#import <Foundation/Foundation.h>
#import <TutkPlayer/TutkPlayer.h>

wxMediaCtrl2::wxMediaCtrl2(wxWindow * parent)
    : wxWindow(parent, wxID_ANY)
{
    NSView * imageView = (NSView *) GetHandle();
    imageView.layer = [[CALayer alloc] init];
    imageView.wantsLayer = YES;
    m_player = [[TutkPlayer alloc] initWithImageView: imageView];
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
    //wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
    //wxPostEvent(this, event);
}

void wxMediaCtrl2::Stop()
{
    TutkPlayer * player2 = (TutkPlayer *) m_player;
    [player2 stop];
}

wxSize wxMediaCtrl2::DoGetBestSize() const
{
    TutkPlayer * player2 = (TutkPlayer *) m_player;
    NSSize size = [player2 videoSize];
    return {(int) size.width, (int) size.height};
}
