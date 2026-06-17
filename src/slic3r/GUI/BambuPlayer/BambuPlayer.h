//
//  BambuPlayer.h
//  BambuPlayer
//
//  Created by cmguo on 2021/12/6.
//

#import <Foundation/Foundation.h>
#import <AVFoundation/AVSampleBufferDisplayLayer.h>
#import <Cocoa/Cocoa.h>

typedef struct __PlayerEventC PlayerEventC;

typedef struct {
    int64_t first_packet_ms;
    int64_t decode_ms;
    int64_t render_ms;
    int     codec;
    int     width;
    int     height;
} BambuFirstFrameInfo;

typedef struct {
    long long session_duration_ms;
    long long freeze_total_ms;
    int       freeze_count;
    float     avg_fps;
    float     avg_bitrate_kbps;
    float     avg_jitter_ms;
    float     max_jitter_ms;
} BambuSessionEndInfo;

NS_ASSUME_NONNULL_BEGIN

@interface BambuPlayer : NSObject

+ (void) initialize;

- (instancetype) initWithDisplayLayer: (AVSampleBufferDisplayLayer*) layer;
- (instancetype) initWithImageView: (NSView*) view;
- (int) open: (char const *) url;
- (NSSize) videoSize;
- (int) play;
- (void) stop;
- (void) close;

- (void) setLogger: (void (*)(void const * context, int level, char const * msg)) logger withContext: (void const *) context;

- (void) setTrackReporter: (void (*)(void* ctx, const PlayerEventC* event)) reporter
              withContext: (void*) ctx;

- (void) setFirstFrameCallback: (void (*)(void const* ctx, const BambuFirstFrameInfo* info)) cb
                   withContext: (void const*) ctx;

- (void) setSessionEndCallback: (void (*)(void const* ctx, const BambuSessionEndInfo* info)) cb
                   withContext: (void const*) ctx;

@end

NS_ASSUME_NONNULL_END
