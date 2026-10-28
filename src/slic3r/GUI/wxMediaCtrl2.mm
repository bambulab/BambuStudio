//
//  wxMediaCtrl2.m
//  BambuStudio
//
//  Created by cmguo on 2021/12/7.
//

#import "wxMediaCtrl2.h"
#import "wx/mediactrl.h"

#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import "BambuPlayer/BambuPlayer.h"
#import "../Utils/NetworkAgent.hpp"

#include <stdlib.h>
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include "Printer/LiveViewTrackContext.h"
#include "BitmapCache.hpp"
#include "I18N.hpp"
#include "libslic3r/Log.hpp"

wxDEFINE_EVENT(EVT_MEDIA_CTRL_STAT, wxCommandEvent);

#define BAMBU_DYNAMIC

// Convert wxImage to CGImageRef. Caller must CGImageRelease the result.
static CGImageRef createCGImageFromWxImage(const wxImage &image)
{
    if (!image.IsOk())
        return nullptr;

    int width  = image.GetWidth();
    int height = image.GetHeight();
    bool hasAlpha = image.HasAlpha();
    int components = hasAlpha ? 4 : 3;
    int bytesPerRow = width * components;

    // Build an interleaved RGBA/RGB buffer
    size_t bufferSize = (size_t)bytesPerRow * height;
    const unsigned char *rgb = image.GetData();
    const unsigned char *alpha = hasAlpha ? image.GetAlpha() : nullptr;

    // Use CFData which retains ownership of the bytes
    CFMutableDataRef cfData = CFDataCreateMutable(kCFAllocatorDefault, bufferSize);
    CFDataSetLength(cfData, bufferSize);
    unsigned char *buffer = CFDataGetMutableBytePtr(cfData);

    for (int i = 0; i < width * height; ++i) {
        buffer[i * components + 0] = rgb[i * 3 + 0];
        buffer[i * components + 1] = rgb[i * 3 + 1];
        buffer[i * components + 2] = rgb[i * 3 + 2];
        if (hasAlpha)
            buffer[i * components + 3] = alpha[i];
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo = hasAlpha
        ? (kCGBitmapByteOrderDefault | kCGImageAlphaLast)
        : (kCGBitmapByteOrderDefault | kCGImageAlphaNone);

    // CGDataProviderCreateWithCFData retains the CFData, so data lives as long as the provider
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(cfData);
    CFRelease(cfData);

    CGImageRef cgImage = CGImageCreate(
        width, height,
        8,                    // bits per component
        8 * components,       // bits per pixel
        bytesPerRow,
        colorSpace,
        bitmapInfo,
        provider,
        nullptr,              // decode array
        false,                // should interpolate
        kCGRenderingIntentDefault);

    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
    return cgImage;
}

// CATextLayer subclass that vertically centers its text.
// CATextLayer draws from the top by default; this override translates the
// graphics context so the text appears vertically centered in the layer bounds.
@interface CenteredCATextLayer : CATextLayer
@end

@implementation CenteredCATextLayer
- (void)drawInContext:(CGContextRef)ctx
{
    // Measure the actual text height
    CGFloat textHeight = self.fontSize;
    if (self.string) {
        NSString *str = nil;
        if ([self.string isKindOfClass:[NSAttributedString class]])
            str = [(NSAttributedString *)self.string string];
        else if ([self.string isKindOfClass:[NSString class]])
            str = (NSString *)self.string;
        if (str) {
            NSFont *font = (__bridge NSFont *)self.font;
            if (!font)
                font = [NSFont systemFontOfSize:self.fontSize];
            NSDictionary *attrs = @{NSFontAttributeName: [NSFont fontWithDescriptor:font.fontDescriptor size:self.fontSize]};
            NSSize size = [str sizeWithAttributes:attrs];
            textHeight = size.height;
        }
    }

    // CATextLayer renders text from the top of its bounds.  In the CG context
    // provided to drawInContext:, a positive Y translation moves the origin up,
    // which shifts the rendered text downward toward the vertical center.
    CGFloat yOffset = (self.bounds.size.height - textHeight) / 2.0;
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, 0.0, yOffset);
    [super drawInContext:ctx];
    CGContextRestoreGState(ctx);
}
@end

void wxMediaCtrl2::bambu_log(void const * ctx, int level, char const * msg)
{
    if (level == 1) {
        wxString msg2(msg);
        if (msg2.EndsWith("]")) {
            int n = msg2.find_last_of('[');
            if (n != wxString::npos) {
                long val = 0;
                wxMediaCtrl2 * ctrl = (wxMediaCtrl2 *) ctx;
                if (msg2.SubString(n + 1, msg2.Length() - 2).ToLong(&val))
                    ctrl->m_error = (int) val;
            }
        } else if (strstr(msg, "stat_log")) {
            wxMediaCtrl2 * ctrl = (wxMediaCtrl2 *) ctx;
            wxCommandEvent evt(EVT_MEDIA_CTRL_STAT);
            evt.SetEventObject(ctrl);
            evt.SetString(strchr(msg, ' ') + 1);
            wxPostEvent(ctrl, evt);
        }
    } else if (level < 0) {
        wxMediaCtrl2 * ctrl = (wxMediaCtrl2 *) ctx;
        ctrl->NotifyStopped();
    }
    BOOST_LOG_TRIVIAL(info) << msg;
}

static void on_player_track_event(void* ctx, const PlayerEventC* event)
{
    if (event == nullptr || event->event_name == nullptr)
        return;

    auto* channel = static_cast<const BambuLiveViewTrack::ChannelInfo*>(ctx);

    BambuLiveViewTrack::EmitParams params;
    if (event->module)        params.module        = event->module;
    if (event->phase)         params.phase         = event->phase;
    if (event->result)        params.result        = event->result;
    if (event->error_code)    params.error_code    = event->error_code;
    if (event->error_message) params.error_message = event->error_message;
    if (event->event_data_body) {
        params.event_data = nlohmann::json::parse(event->event_data_body, nullptr, false);
        if (params.event_data.is_discarded())
            params.event_data = nlohmann::json::object();
    }

    BambuLiveViewTrack::LiveViewTrackContext::instance()
        .emit(event->event_name, params, channel);
}

static void on_first_frame(void const* ctx, const BambuFirstFrameInfo* ff)
{
    wxMediaCtrl2* ctrl = (wxMediaCtrl2*) ctx;
    FirstFrameInfo& info = ctrl->m_first_frame_info;
    info.first_packet_time_ms       = ff->first_packet_ms;
    info.decode_first_frame_time_ms = ff->decode_ms;
    info.render_first_frame_time_ms = ff->render_ms;
    info.video_codec                = ff->codec;
    info.resolution_width           = ff->width;
    info.resolution_height          = ff->height;

    auto play_start = ctrl->m_play_start_time;
    if (play_start != std::chrono::system_clock::time_point{}) {
        auto now = std::chrono::system_clock::now();
        info.first_frame_cost_ms = (int) std::chrono::duration_cast<std::chrono::milliseconds>(
            now - play_start).count();
    }

    ctrl->CallAfter([ctrl] {
        wxCommandEvent evt(EVT_MEDIA_CTRL_FIRST_FRAME);
        evt.SetEventObject(ctrl);
        evt.SetInt(ctrl->m_first_frame_info.first_frame_cost_ms);
        evt.SetClientData(new FirstFrameInfo(ctrl->m_first_frame_info));
        wxPostEvent(ctrl, evt);
    });
}

static void on_session_end(void const* ctx, const BambuSessionEndInfo* se)
{
    wxMediaCtrl2* ctrl = (wxMediaCtrl2*) ctx;
    ctrl->m_session_stat.session_duration_ms      = se->session_duration_ms;
    ctrl->m_session_stat.avg_fps                  = se->avg_fps;
    ctrl->m_session_stat.avg_bitrate_kbps         = se->avg_bitrate_kbps;
    ctrl->m_session_stat.freeze_count             = se->freeze_count;
    ctrl->m_session_stat.freeze_total_duration_ms = se->freeze_total_ms;
    ctrl->m_session_stat.avg_jitter_ms            = se->avg_jitter_ms;
    ctrl->m_session_stat.max_jitter_ms            = se->max_jitter_ms;

    Bambu_SessionStat stat = ctrl->m_session_stat;
    ctrl->CallAfter([ctrl, stat] {
        wxCommandEvent evt(EVT_MEDIA_CTRL_SESSION_END);
        evt.SetEventObject(ctrl);
        evt.SetClientData(new Bambu_SessionStat(stat));
        wxPostEvent(ctrl, evt);
    });
}

wxMediaCtrl2::wxMediaCtrl2(wxWindow * parent)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    NSView * imageView = (NSView *) GetHandle();
    imageView.layer = [[CALayer alloc] init];
    CGColorRef color = CGColorCreateGenericRGB(0, 0, 0, 1.0f);
    imageView.layer.backgroundColor = color;
    CGColorRelease(color);
    imageView.layer.masksToBounds = YES;
    imageView.wantsLayer = YES;
    Bind(wxEVT_MOUSEWHEEL, &wxMediaCtrl2::mouseWheelEvent, this);
    create_player();
}

wxMediaCtrl2::~wxMediaCtrl2()
{
    CATextLayer *wmLayer = (CATextLayer *)m_watermark_layer;
    if (wmLayer) {
        [wmLayer removeFromSuperlayer];
        [wmLayer release];
        m_watermark_layer = nullptr;
    }
    CALayer *badgeBgLayer = (CALayer *)m_badge_bg_layer;
    if (badgeBgLayer) {
        [badgeBgLayer removeFromSuperlayer];
        [badgeBgLayer release];
        m_badge_bg_layer = nullptr;
    }
    CALayer *badgeDotLayer = (CALayer *)m_badge_dot_layer;
    if (badgeDotLayer) {
        [badgeDotLayer removeFromSuperlayer];
        [badgeDotLayer release];
        m_badge_dot_layer = nullptr;
    }
    CALayer *zoomBadgeBgLayer = (CALayer *)m_zoom_badge_bg_layer;
    if (zoomBadgeBgLayer) {
        [zoomBadgeBgLayer removeFromSuperlayer];
        [zoomBadgeBgLayer release];
        m_zoom_badge_bg_layer = nullptr;
    }
    CATextLayer *zoomBadgeTextLayer = (CATextLayer *)m_zoom_badge_text_layer;
    if (zoomBadgeTextLayer) {
        [zoomBadgeTextLayer removeFromSuperlayer];
        [zoomBadgeTextLayer release];
        m_zoom_badge_text_layer = nullptr;
    }
    CALayer *pauseIconLayer = (CALayer *)m_pause_icon_layer;
    if (pauseIconLayer) {
        [pauseIconLayer removeFromSuperlayer];
        [pauseIconLayer release];
        m_pause_icon_layer = nullptr;
    }
    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (idleLayer) {
        [idleLayer removeFromSuperlayer];
        [idleLayer release];
        m_idle_layer = nullptr;
    }
    BambuPlayer * player = (BambuPlayer *) m_player;
    [player dealloc];
    AVSampleBufferDisplayLayer *videoLayer = (AVSampleBufferDisplayLayer *)m_video_layer;
    if (videoLayer) {
        [videoLayer removeFromSuperlayer];
        [videoLayer release];
        m_video_layer = nullptr;
    }
}

void wxMediaCtrl2::create_player()
{
	auto module = Slic3r::NetworkAgent::get_bambu_source_entry();
	if (!module) {
		//not ready yet
		BOOST_LOG_TRIVIAL(info) << "Network plugin not ready currently!";
		return;
	}
    Class cls = (__bridge Class) dlsym(module, "OBJC_CLASS_$_BambuPlayer");
    if (cls == nullptr) {
        m_error = -2;
        return;
    }
    NSView * imageView = (NSView *) GetHandle();
    AVSampleBufferDisplayLayer *videoLayer = (AVSampleBufferDisplayLayer *)m_video_layer;
    if (!videoLayer) {
        videoLayer = [[AVSampleBufferDisplayLayer alloc] init];
        videoLayer.frame = imageView.layer.bounds;
        videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;
        [imageView.layer insertSublayer:videoLayer atIndex:0];
        m_video_layer = videoLayer;
    }
    BambuPlayer * player = [cls alloc];
    [player initWithDisplayLayer: videoLayer];
    [player setLogger: bambu_log withContext: this];
    if ([player respondsToSelector:@selector(setTrackReporter:withContext:)])
        [player setTrackReporter: on_player_track_event withContext: &m_track_channel];
    if ([player respondsToSelector:@selector(setFirstFrameCallback:withContext:)])
        [player setFirstFrameCallback: on_first_frame withContext: this];
    if ([player respondsToSelector:@selector(setSessionEndCallback:withContext:)])
        [player setSessionEndCallback: on_session_end withContext: this];
    m_player = player;
}

void wxMediaCtrl2::Load(wxURI url, std::chrono::system_clock::time_point play_start_time)
{
	if (!m_player) {
		create_player();
		if (!m_player) {
			BOOST_LOG_TRIVIAL(info) << "create_player failed currently!";
			return;
		}
	}

    resetZoom();
    m_play_start_time  = play_start_time;
    m_first_frame_info = FirstFrameInfo{};
    m_session_stat     = Bambu_SessionStat{};
    BambuPlayer * player = (BambuPlayer *) m_player;
    if (player) {
        [player close];
        m_error = 0;
        m_error = [player open: url.BuildURI().ToUTF8()];
        if (m_error == 0 && m_play_start_time != std::chrono::system_clock::time_point{}) {
            auto now = std::chrono::system_clock::now();
            int ms = (int) std::chrono::duration_cast<std::chrono::milliseconds>(now - m_play_start_time).count();
            CallAfter([this, ms] {
                wxCommandEvent evt(EVT_MEDIA_CTRL_FIRST_FRAME);
                evt.SetEventObject(this);
                evt.SetInt(ms);
                wxPostEvent(this, evt);
            });
        }
    }
    // Hide idle image when loading video (must run on main thread for CALayer)
    dispatch_async(dispatch_get_main_queue(), ^{ removeIdleLayer(); });
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
			BOOST_LOG_TRIVIAL(info) << "create_player failed currently!";
			return;
		}
	}
    BambuPlayer * player2 = (BambuPlayer *) m_player;
    [player2 play];
    // Hide idle image during playback (must run on main thread for CALayer)
    dispatch_async(dispatch_get_main_queue(), ^{
        removeIdleLayer();
        updateStatusBadge();
    });
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
			BOOST_LOG_TRIVIAL(info) << "create_player failed currently!";
			return;
		}
	}
    resetZoom();
    BambuPlayer * player2 = (BambuPlayer *) m_player;
    [player2 close];
    m_suppress_pause_icon = false;
    dispatch_async(dispatch_get_main_queue(), ^{
        AVSampleBufferDisplayLayer *videoLayer = (AVSampleBufferDisplayLayer *)m_video_layer;
        [videoLayer flushAndRemoveImage];
        updateIdleLayer();
    });
    NotifyStopped();
}

void wxMediaCtrl2::mouseWheelEvent(wxMouseEvent &evt)
{
    const int rotation = evt.GetWheelRotation();
    if (rotation == 0 || m_state != wxMEDIASTATE_PLAYING) {
        evt.Skip();
        return;
    }

    const int wheel_delta = evt.GetWheelDelta();
    const double steps = wheel_delta == 0
        ? (rotation > 0 ? 1.0 : -1.0)
        : static_cast<double>(rotation) / wheel_delta;
    const double old_zoom = m_zoom;
    const double new_zoom = std::clamp(old_zoom * std::pow(1.1, steps), 1.0, 5.0);
    if (new_zoom == old_zoom)
        return;

    const wxSize size = GetClientSize();
    const wxPoint cursor = evt.GetPosition();
    const double cursor_x = cursor.x - size.x / 2.0;
    const double cursor_y = cursor.y - size.y / 2.0;
    const double ratio = new_zoom / old_zoom;

    m_zoom_offset_x = cursor_x - ratio * (cursor_x - m_zoom_offset_x);
    m_zoom_offset_y = cursor_y - ratio * (cursor_y - m_zoom_offset_y);
    m_zoom = new_zoom;
    clampZoomOffset();
    applyVideoTransform();
}

void wxMediaCtrl2::resetZoom()
{
    m_zoom = 1.0;
    m_zoom_offset_x = 0.0;
    m_zoom_offset_y = 0.0;
    applyVideoTransform();
}

void wxMediaCtrl2::clampZoomOffset()
{
    const wxSize view_size = GetClientSize();
    wxSize video_size = GetVideoSize();
    if (video_size.x <= 0 || video_size.y <= 0)
        video_size = m_video_size;
    if (view_size.x <= 0 || view_size.y <= 0 || video_size.x <= 0 || video_size.y <= 0) {
        m_zoom_offset_x = 0.0;
        m_zoom_offset_y = 0.0;
        return;
    }

    double fitted_width;
    double fitted_height;
    if (static_cast<double>(video_size.x) * view_size.y <
        static_cast<double>(video_size.y) * view_size.x) {
        fitted_height = view_size.y;
        fitted_width = static_cast<double>(video_size.x) * view_size.y / video_size.y;
    } else {
        fitted_width = view_size.x;
        fitted_height = static_cast<double>(video_size.y) * view_size.x / video_size.x;
    }

    const double max_x = std::max(0.0, (fitted_width * m_zoom - view_size.x) / 2.0);
    const double max_y = std::max(0.0, (fitted_height * m_zoom - view_size.y) / 2.0);
    m_zoom_offset_x = std::clamp(m_zoom_offset_x, -max_x, max_x);
    m_zoom_offset_y = std::clamp(m_zoom_offset_y, -max_y, max_y);
}

void wxMediaCtrl2::applyVideoTransform()
{
    AVSampleBufferDisplayLayer *videoLayer = (AVSampleBufferDisplayLayer *)m_video_layer;
    if (!videoLayer)
        return;

    const double layer_offset_y = [videoLayer.superlayer isGeometryFlipped]
        ? m_zoom_offset_y
        : -m_zoom_offset_y;
    const CGRect bounds = videoLayer.superlayer.bounds;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    videoLayer.transform = CATransform3DMakeScale(m_zoom, m_zoom, 1.0);
    videoLayer.position = CGPointMake(
        CGRectGetMidX(bounds) + m_zoom_offset_x,
        CGRectGetMidY(bounds) + layer_offset_y);
    [CATransaction commit];
    updateZoomBadge();
}

void wxMediaCtrl2::SetIdleImage(wxString const &image, wxString const &watermark_text)
{
    if (m_idle_image == image && m_watermark_text == watermark_text)
        return;
    m_idle_image = image;
    m_watermark_text = watermark_text;

    // Clear stale wxImage contents so updateIdleLayer reloads from the new file path
    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (idleLayer)
        idleLayer.contents = nil;

    if (m_state != wxMEDIASTATE_PLAYING) {
        updateIdleLayer();
    }
}

void wxMediaCtrl2::SetIdleImage(const wxImage &image, wxString const &watermark_text)
{
    if (!image.IsOk())
        return;
    m_idle_image.clear();
    m_watermark_text = watermark_text;
    if (m_state != wxMEDIASTATE_PLAYING) {
        // Convert wxImage to CGImage and display
        CGImageRef cgImage = createCGImageFromWxImage(image);
        if (!cgImage)
            return;

        NSView *view = (NSView *)GetHandle();
        CALayer *rootLayer = view.layer;
        if (!rootLayer) {
            CGImageRelease(cgImage);
            return;
        }

        // Create or reuse idle layer
        CALayer *idleLayer = (CALayer *)m_idle_layer;
        if (!idleLayer) {
            idleLayer = [[CALayer alloc] init];
            idleLayer.contentsGravity = kCAGravityResizeAspect;
            m_idle_layer = idleLayer;
        }
        idleLayer.frame = rootLayer.bounds;
        idleLayer.contents = (__bridge id)cgImage;
        idleLayer.hidden = NO;
        CGImageRelease(cgImage);

        if (idleLayer.superlayer != rootLayer)
            [rootLayer addSublayer:idleLayer];

        updatePauseIconLayer();
        updateStatusBadge();
    }
}

void wxMediaCtrl2::updateIdleLayer()
{
    NSView *view = (NSView *)GetHandle();
    CALayer *rootLayer = view.layer;
    if (!rootLayer)
        return;

    // Create or reuse idle layer
    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (!idleLayer) {
        idleLayer = [[CALayer alloc] init];
        idleLayer.contentsGravity = kCAGravityResizeAspect;
        m_idle_layer = idleLayer;
    }
    idleLayer.frame = rootLayer.bounds;

    // Load image from file path
    if (!m_idle_image.empty()) {
        NSString *path = [NSString stringWithUTF8String:m_idle_image.ToUTF8().data()];
        NSImage *nsImage = [[NSImage alloc] initWithContentsOfFile:path];
        if (nsImage) {
            CGImageRef cgImage = [nsImage CGImageForProposedRect:nil context:nil hints:nil];
            idleLayer.contents = (__bridge id)cgImage;
            [nsImage release];
        } else {
            BOOST_LOG_TRIVIAL(warning) << "failed to load image: " << m_idle_image.ToUTF8().data();
            idleLayer.contents = nil;
        }
    } else {
        // No file path — contents may have been set directly by SetIdleImage(wxImage)
        if (!idleLayer.contents) {
            idleLayer.hidden = YES;
            return;
        }
    }

    idleLayer.hidden = NO;
    if (idleLayer.superlayer != rootLayer)
        [rootLayer addSublayer:idleLayer];

    updatePauseIconLayer();
    updateStatusBadge();
}

void wxMediaCtrl2::updateStatusBadge()
{
    NSView *view = (NSView *)GetHandle();
    CALayer *rootLayer = view.layer;
    if (!rootLayer)
        return;

    bool playing = (m_state == wxMEDIASTATE_PLAYING);
    wxString text = playing ? _L("Live") : m_watermark_text;
    bool visible = playing || !m_watermark_text.empty();

    CALayer *bgLayer = (CALayer *)m_badge_bg_layer;
    CALayer *dotLayer = (CALayer *)m_badge_dot_layer;
    CenteredCATextLayer *textLayer = (CenteredCATextLayer *)m_watermark_layer;

    if (!visible) {
        if (bgLayer) bgLayer.hidden = YES;
        if (dotLayer) dotLayer.hidden = YES;
        if (textLayer) textLayer.hidden = YES;
        return;
    }

    if (!bgLayer) {
        bgLayer = [[CALayer alloc] init];
        m_badge_bg_layer = bgLayer;
    }
    if (!dotLayer) {
        dotLayer = [[CALayer alloc] init];
        m_badge_dot_layer = dotLayer;
    }
    if (!textLayer) {
        textLayer = [[CenteredCATextLayer alloc] init];
        textLayer.alignmentMode = kCAAlignmentLeft;
        textLayer.contentsScale = [[NSScreen mainScreen] backingScaleFactor];
        m_watermark_layer = textLayer;
    }

    NSString *nsText = [NSString stringWithUTF8String:text.ToUTF8().data()];
    NSFont *font = [NSFont systemFontOfSize:14.0];
    NSDictionary *attrs = @{NSFontAttributeName: font};
    NSSize textSize = [nsText sizeWithAttributes:attrs];

    CGFloat padH   = 12.0;
    CGFloat dotD   = 8.0;
    CGFloat dotGap = 6.0;
    CGFloat pillH  = 39.0;
    CGFloat margin = 10.0;
    CGFloat pillW  = padH + dotD + dotGap + textSize.width + padH;
    CGFloat radius = pillH / 2.0;

    CGFloat pillX = margin;
    CGFloat pillY = margin;

    bgLayer.frame = CGRectMake(pillX, pillY, pillW, pillH);
    bgLayer.cornerRadius = radius;
    CGColorRef bgColor = CGColorCreateGenericRGB(51 / 255.0, 51 / 255.0, 51 / 255.0, 160 / 255.0);
    bgLayer.backgroundColor = bgColor;
    CGColorRelease(bgColor);
    bgLayer.hidden = NO;
    if (bgLayer.superlayer != rootLayer)
        [rootLayer addSublayer:bgLayer];

    CGFloat dotX = pillX + padH;
    CGFloat dotY = pillY + (pillH - dotD) / 2.0;
    dotLayer.frame = CGRectMake(dotX, dotY, dotD, dotD);
    dotLayer.cornerRadius = dotD / 2.0;
    wxColour dotColour = playing ? wxColour(76, 217, 100) : wxColour(0xCE, 0xCE, 0xCE);
    CGColorRef dotColor = CGColorCreateGenericRGB(dotColour.Red() / 255.0, dotColour.Green() / 255.0, dotColour.Blue() / 255.0, 1.0);
    dotLayer.backgroundColor = dotColor;
    CGColorRelease(dotColor);
    dotLayer.hidden = NO;
    if (dotLayer.superlayer != rootLayer)
        [rootLayer addSublayer:dotLayer];

    CGFloat textX = dotX + dotD + dotGap;
    textLayer.frame = CGRectMake(textX, pillY, textSize.width, pillH);
    textLayer.string = nsText;
    textLayer.font = (__bridge CFTypeRef)font;
    textLayer.fontSize = 14.0;
    textLayer.backgroundColor = nil;
    CGColorRef fgColor = CGColorCreateGenericRGB(0xCE / 255.0, 0xCE / 255.0, 0xCE / 255.0, 1.0);
    textLayer.foregroundColor = fgColor;
    CGColorRelease(fgColor);
    textLayer.hidden = NO;
    if (textLayer.superlayer != rootLayer)
        [rootLayer addSublayer:textLayer];
}

void wxMediaCtrl2::updateZoomBadge()
{
    NSView *view = (NSView *)GetHandle();
    CALayer *rootLayer = view.layer;
    if (!rootLayer)
        return;

    CALayer *bgLayer = (CALayer *)m_zoom_badge_bg_layer;
    CenteredCATextLayer *textLayer = (CenteredCATextLayer *)m_zoom_badge_text_layer;
    if (m_zoom <= 1.0) {
        if (bgLayer) bgLayer.hidden = YES;
        if (textLayer) textLayer.hidden = YES;
        return;
    }

    if (!bgLayer) {
        bgLayer = [[CALayer alloc] init];
        m_zoom_badge_bg_layer = bgLayer;
    }
    if (!textLayer) {
        textLayer = [[CenteredCATextLayer alloc] init];
        textLayer.alignmentMode = kCAAlignmentLeft;
        textLayer.contentsScale = [[NSScreen mainScreen] backingScaleFactor];
        m_zoom_badge_text_layer = textLayer;
    }

    wxString text = _L("Zoom") + wxString::Format(" %.1fx", m_zoom);
    NSString *nsText = [NSString stringWithUTF8String:text.ToUTF8().data()];
    NSFont *font = [NSFont systemFontOfSize:14.0];
    NSDictionary *attrs = @{NSFontAttributeName: font};
    NSSize textSize = [nsText sizeWithAttributes:attrs];

    CGFloat padH = 12.0;
    CGFloat pillH = 39.0;
    CGFloat margin = 10.0;
    CGFloat pillW = padH + textSize.width + padH;
    CGFloat pillX = rootLayer.bounds.size.width - margin - pillW;
    CGFloat pillY = margin;

    bgLayer.frame = CGRectMake(pillX, pillY, pillW, pillH);
    bgLayer.cornerRadius = pillH / 2.0;
    CGColorRef bgColor = CGColorCreateGenericRGB(51 / 255.0, 51 / 255.0, 51 / 255.0, 160 / 255.0);
    bgLayer.backgroundColor = bgColor;
    CGColorRelease(bgColor);
    bgLayer.hidden = NO;
    if (bgLayer.superlayer != rootLayer)
        [rootLayer addSublayer:bgLayer];

    textLayer.frame = CGRectMake(pillX + padH, pillY, textSize.width, pillH);
    textLayer.string = nsText;
    textLayer.font = (__bridge CFTypeRef)font;
    textLayer.fontSize = 14.0;
    textLayer.backgroundColor = nil;
    CGColorRef fgColor = CGColorCreateGenericRGB(0xCE / 255.0, 0xCE / 255.0, 0xCE / 255.0, 1.0);
    textLayer.foregroundColor = fgColor;
    CGColorRelease(fgColor);
    textLayer.hidden = NO;
    if (textLayer.superlayer != rootLayer)
        [rootLayer addSublayer:textLayer];
}

void wxMediaCtrl2::updatePauseIconLayer()
{
    if (m_suppress_pause_icon) {
        CALayer *pauseIconLayer = (CALayer *)m_pause_icon_layer;
        if (pauseIconLayer)
            pauseIconLayer.hidden = YES;
        return;
    }

    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (!idleLayer)
        return;

    CGFloat iconSz = idleLayer.bounds.size.height * 0.12;
    if (iconSz <= 0)
        return;

    static Slic3r::GUI::BitmapCache cache;
    wxBitmap *bmp = cache.load_svg("liveview_pause", (unsigned) std::lround(iconSz), (unsigned) std::lround(iconSz));
    if (!bmp || !bmp->IsOk())
        return;

    wxImage image = bmp->ConvertToImage();
    CGImageRef cgImage = createCGImageFromWxImage(image);
    if (!cgImage)
        return;

    CALayer *iconLayer = (CALayer *)m_pause_icon_layer;
    if (!iconLayer) {
        iconLayer = [[CALayer alloc] init];
        iconLayer.contentsGravity = kCAGravityResizeAspect;
        m_pause_icon_layer = iconLayer;
    }

    CGFloat iconX = (idleLayer.bounds.size.width - iconSz) / 2.0;
    CGFloat iconY = (idleLayer.bounds.size.height - iconSz) / 2.0;
    iconLayer.frame = CGRectMake(iconX, iconY, iconSz, iconSz);
    iconLayer.contentsScale = cache.scale();
    iconLayer.contents = (__bridge id)cgImage;
    iconLayer.hidden = NO;
    CGImageRelease(cgImage);

    if (iconLayer.superlayer != idleLayer)
        [idleLayer addSublayer:iconLayer];
}

void wxMediaCtrl2::SuppressPauseIcon(bool suppress)
{
    if (m_suppress_pause_icon == suppress)
        return;
    m_suppress_pause_icon = suppress;
    dispatch_async(dispatch_get_main_queue(), ^{
        CALayer *pauseIconLayer = (CALayer *)m_pause_icon_layer;
        if (pauseIconLayer)
            pauseIconLayer.hidden = m_suppress_pause_icon ? YES : NO;
    });
}

void wxMediaCtrl2::removeIdleLayer()
{
    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (idleLayer)
        idleLayer.hidden = YES;
    CALayer *pauseIconLayer = (CALayer *)m_pause_icon_layer;
    if (pauseIconLayer)
        pauseIconLayer.hidden = YES;
    CALayer *badgeBgLayer = (CALayer *)m_badge_bg_layer;
    if (badgeBgLayer)
        badgeBgLayer.hidden = YES;
    CALayer *badgeDotLayer = (CALayer *)m_badge_dot_layer;
    if (badgeDotLayer)
        badgeDotLayer.hidden = YES;
    CATextLayer *wmLayer = (CATextLayer *)m_watermark_layer;
    if (wmLayer)
        wmLayer.hidden = YES;
}

void wxMediaCtrl2::NotifyStopped()
{
    if (m_state != wxMEDIASTATE_STOPPED) {
        m_state = wxMEDIASTATE_STOPPED;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
        dispatch_async(dispatch_get_main_queue(), ^{ updateStatusBadge(); });
    }
}

void wxMediaCtrl2::SetTrackChannel(const BambuLiveViewTrack::ChannelInfo& info)
{
    m_track_channel = info;
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
        if (size.width > 0)
            const_cast<wxSize&>(m_video_size) = {(int) size.width, (int) size.height};
        return {(int) size.width, (int) size.height};
    } else {
        return {0, 0};
    }
}

void wxMediaCtrl2::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    NSView *view = (NSView *)GetHandle();
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);

    CALayer *rootLayer = view.layer;
    AVSampleBufferDisplayLayer *videoLayer = (AVSampleBufferDisplayLayer *)m_video_layer;
    if (rootLayer && videoLayer) {
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        videoLayer.transform = CATransform3DIdentity;
        videoLayer.frame = rootLayer.bounds;
        [CATransaction commit];
    }

    if (sizeFlags & wxSIZE_USE_EXISTING) {
        clampZoomOffset();
        applyVideoTransform();
        return;
    }
    wxMediaCtrl_OnSize(this, m_video_size, width, height);

    if (!rootLayer) return;

    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (idleLayer && !idleLayer.hidden) {
        idleLayer.frame = rootLayer.bounds;
        updatePauseIconLayer();
    }
    clampZoomOffset();
    applyVideoTransform();
}
