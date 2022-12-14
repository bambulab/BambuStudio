//
//  wxMediaCtrl2.m
//  BambuStudio
//
//  Created by cmguo on 2021/12/7.
//

#import "wxMediaCtrl2.h"
#import "wx/mediactrl.h"
#include <boost/log/trivial.hpp>

#import <Foundation/Foundation.h>
#import "BambuPlayer/BambuPlayer.h"
#import "../Utils/NetworkAgent.hpp"

#include <stdlib.h>
#include <dlfcn.h>

#define BAMBU_DYNAMIC

static void bambu_log(void const * ctx, int level, char const * msg)
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

wxMediaCtrl2::wxMediaCtrl2(wxWindow * parent)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    NSView * imageView = (NSView *) GetHandle();
    imageView.layer = [[CALayer alloc] init];
    CGColorRef color = CGColorCreateGenericRGB(0, 0, 0, 1.0f);
    imageView.layer.backgroundColor = color;
    CGColorRelease(color);
    imageView.wantsLayer = YES;
    create_player();
}

wxMediaCtrl2::~wxMediaCtrl2()
{
    BambuPlayer * player = (BambuPlayer *) m_player;
    [player dealloc];
}

void wxMediaCtrl2::create_player()
{
	auto module = Slic3r::NetworkAgent::get_bambu_source_entry();
	if (!module) {
		//not ready yet
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "Network plugin not ready currently!";
		return;
	}
    Class cls = (__bridge Class) dlsym(module, "OBJC_CLASS_$_BambuPlayer");
    if (cls == nullptr) {
        m_error = -2;
        return;
    }
    NSView * imageView = (NSView *) GetHandle();
    BambuPlayer * player = [cls alloc];
    [player initWithImageView: imageView];
    [player setLogger: bambu_log withContext: &m_error];
    m_player = player;
}

void wxMediaCtrl2::Load(wxURI url)
{
	if (!m_player) {
		create_player();
		if (!m_player) {
			BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": create_player failed currently!";
			return;
		}
	}

    BambuPlayer * player = (BambuPlayer *) m_player;
    if (player) {
        [player close];
        [player open: url.BuildURI().ToUTF8()];
        m_error = 0;
    }
    wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
    event.SetId(GetId());
    event.SetEventObject(this);
    wxPostEvent(this, event);
}

void wxMediaCtrl2::Play()
{
	if (!m_player) {
		create_player();
		if (!m_player) {
			BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": create_player failed currently!";
			return;
		}
	}
    BambuPlayer * player2 = (BambuPlayer *) m_player;
    [player2 play];
    if (m_state != wxMEDIASTATE_PLAYING) {
        m_state = wxMEDIASTATE_PLAYING;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
    }
}

void wxMediaCtrl2::Stop()
{
	if (!m_player) {
		create_player();
		if (!m_player) {
			BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": create_player failed currently!";
			return;
		}
	}
    BambuPlayer * player2 = (BambuPlayer *) m_player;
    [player2 close];
    if (m_state != wxMEDIASTATE_STOPPED) {
        m_state = wxMEDIASTATE_STOPPED;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
    }
}

wxMediaState wxMediaCtrl2::GetState() const
{
    return m_state;
}

wxSize wxMediaCtrl2::GetVideoSize() const
{
    BambuPlayer * player2 = (BambuPlayer *) m_player;
    if (player2) {
        NSSize size = [player2 videoSize];
        return {(int) size.width, (int) size.height};
    } else {
        return {0, 0};
    }
}
