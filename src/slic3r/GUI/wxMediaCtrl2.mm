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
#import <QuartzCore/QuartzCore.h>
#import "BambuPlayer/BambuPlayer.h"
#import "../Utils/NetworkAgent.hpp"

#include <stdlib.h>
#include <dlfcn.h>

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
    CATextLayer *wmLayer = (CATextLayer *)m_watermark_layer;
    if (wmLayer) {
        [wmLayer removeFromSuperlayer];
        [wmLayer release];
        m_watermark_layer = nullptr;
    }
    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (idleLayer) {
        [idleLayer removeFromSuperlayer];
        [idleLayer release];
        m_idle_layer = nullptr;
    }
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
    [player setLogger: bambu_log withContext: this];
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
        m_error = 0;
        m_error = [player open: url.BuildURI().ToUTF8()];
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
			BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": create_player failed currently!";
			return;
		}
	}
    BambuPlayer * player2 = (BambuPlayer *) m_player;
    [player2 play];
    // Hide idle image during playback (must run on main thread for CALayer)
    dispatch_async(dispatch_get_main_queue(), ^{ removeIdleLayer(); });
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
    // Restore idle image after stopping (must run on main thread for CALayer)
    dispatch_async(dispatch_get_main_queue(), ^{ updateIdleLayer(); });
    NotifyStopped();
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

        updateWatermarkLayer();
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
            BOOST_LOG_TRIVIAL(warning) << "wxMediaCtrl2::updateIdleLayer: failed to load image: " << m_idle_image.ToUTF8().data();
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

    updateWatermarkLayer();
}

void wxMediaCtrl2::updateWatermarkLayer()
{
    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (!idleLayer || idleLayer.hidden)
        return;

    if (!m_watermark_text.empty()) {
        CenteredCATextLayer *wmLayer = (CenteredCATextLayer *)m_watermark_layer;
        if (!wmLayer) {
            wmLayer = [[CenteredCATextLayer alloc] init];
            wmLayer.alignmentMode = kCAAlignmentCenter;
            wmLayer.contentsScale = [[NSScreen mainScreen] backingScaleFactor];
            m_watermark_layer = wmLayer;
        }

        NSString *wmText = [NSString stringWithUTF8String:m_watermark_text.ToUTF8().data()];
        NSFont *font = [NSFont boldSystemFontOfSize:10.0];
        NSDictionary *attrs = @{NSFontAttributeName: font};
        NSSize textSize = [wmText sizeWithAttributes:attrs];

        CGFloat padH = 12.0;
        CGFloat padV = 8.0;
        CGFloat wmW = textSize.width + 2 * padH;
        CGFloat wmH = textSize.height + 2 * padV;

        NSView *view = (NSView *)GetHandle();
        CALayer *rootLayer = view.layer;
        CGFloat parentH = idleLayer.bounds.size.height;
        CGFloat wmX = rootLayer ? (rootLayer.bounds.size.width - wmW) / 2.0 : 0;
        // wxWidgets NSView is flipped (y=0 at top), so place near bottom edge
        CGFloat wmY = parentH - wmH - 10.0;
        CGFloat radius = 8.0;

        wmLayer.frame = CGRectMake(wmX, wmY, wmW, wmH);
        wmLayer.cornerRadius = radius;

        CGColorRef bgColor = CGColorCreateGenericRGB(0.2, 0.2, 0.2, 0.63);
        wmLayer.backgroundColor = bgColor;
        CGColorRelease(bgColor);

        wmLayer.string = wmText;
        wmLayer.font = (__bridge CFTypeRef)font;
        wmLayer.fontSize = 10.0;

        CGColorRef fgColor = CGColorCreateGenericRGB(0.86, 0.86, 0.86, 1.0);
        wmLayer.foregroundColor = fgColor;
        CGColorRelease(fgColor);

        wmLayer.hidden = NO;

        if (wmLayer.superlayer != idleLayer)
            [idleLayer addSublayer:wmLayer];
    } else {
        CATextLayer *wmLayer = (CATextLayer *)m_watermark_layer;
        if (wmLayer)
            wmLayer.hidden = YES;
    }
}

void wxMediaCtrl2::removeIdleLayer()
{
    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (idleLayer)
        idleLayer.hidden = YES;
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
        if (size.width > 0)
            const_cast<wxSize&>(m_video_size) = {(int) size.width, (int) size.height};
        return {(int) size.width, (int) size.height};
    } else {
        return {0, 0};
    }
}

void wxMediaCtrl2::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    if (sizeFlags & wxSIZE_USE_EXISTING) return;
    wxMediaCtrl_OnSize(this, m_video_size, width, height);

    // Sync idle layer and watermark positions on resize
    NSView *view = (NSView *)GetHandle();
    CALayer *rootLayer = view.layer;
    if (!rootLayer) return;

    CALayer *idleLayer = (CALayer *)m_idle_layer;
    if (idleLayer && !idleLayer.hidden) {
        idleLayer.frame = rootLayer.bounds;

        // Reposition watermark centered at bottom (flipped coords: y=0 at top)
        CATextLayer *wmLayer = (CATextLayer *)m_watermark_layer;
        if (wmLayer && !wmLayer.hidden) {
            CGFloat wmW = wmLayer.frame.size.width;
            CGFloat wmH = wmLayer.frame.size.height;
            CGFloat wmX = (rootLayer.bounds.size.width - wmW) / 2.0;
            CGFloat wmY = idleLayer.bounds.size.height - wmH - 10.0;
            wmLayer.frame = CGRectMake(wmX, wmY, wmW, wmH);
        }
    }
}
