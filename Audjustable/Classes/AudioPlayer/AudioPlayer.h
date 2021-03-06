/**********************************************************************************
 AudioPlayer.m
 
 Created by Thong Nguyen on 14/05/2012.
 https://github.com/tumtumtum/audjustable
 
 Inspired by Matt Gallagher's AudioStreamer:
 https://github.com/mattgallagher/AudioStreamer
 
 Copyright (c) 2012 Thong Nguyen (tumtumtum@gmail.com). All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 1. Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 3. All advertising materials mentioning features or use of this software
 must display the following acknowledgement:
 This product includes software developed by the <organization>.
 4. Neither the name of the <organization> nor the
 names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **********************************************************************************/

#import <Foundation/Foundation.h>
#import <pthread.h>
#import "DataSource.h"
#include <AudioToolbox/AudioToolbox.h>

#define AudioPlayerDefaultNumberOfAudioQueueBuffers (2 * 1024)

typedef enum
{
	AudioPlayerInternalStateInitialised = 0, // 初始化完成
    AudioPlayerInternalStateRunning = 1,  // 运行中
    AudioPlayerInternalStatePlaying = (1 << 1) | AudioPlayerInternalStateRunning,  // 播放中
	AudioPlayerInternalStateStartingThread = (1 << 2) | AudioPlayerInternalStateRunning,  // 启动线程中
	AudioPlayerInternalStateWaitingForData = (1 << 3) | AudioPlayerInternalStateRunning, // 等待数据中
    AudioPlayerInternalStateWaitingForQueueToStart = (1 << 4) | AudioPlayerInternalStateRunning, // 等待AQ启动中
    AudioPlayerInternalStatePaused = (1 << 5) | AudioPlayerInternalStateRunning, // 暂停中
    AudioPlayerInternalStateRebuffering = (1 << 6) | AudioPlayerInternalStateRunning, // 重新缓冲中
    AudioPlayerInternalStateStopping = (1 << 7), // 停止中
    AudioPlayerInternalStateStopped = (1 << 8), // 已停止
    AudioPlayerInternalStateDisposed = (1 << 9), // 已关闭
    AudioPlayerInternalStateError = (1 << 10) // 出错了。
}
AudioPlayerInternalState;

typedef enum
{
    AudioPlayerStateReady,
    AudioPlayerStateRunning = 1,
    AudioPlayerStatePlaying = (1 << 1) | AudioPlayerStateRunning,
    AudioPlayerStatePaused = (1 << 2) | AudioPlayerStateRunning,
    AudioPlayerStateStopped = (1 << 3),
    AudioPlayerStateError = (1 << 4),
    AudioPlayerStateDisposed = (1 << 5)
}
AudioPlayerState;

typedef enum
{
	AudioPlayerStopReasonNoStop = 0,
	AudioPlayerStopReasonEof,
	AudioPlayerStopReasonUserAction,
    AudioPlayerStopReasonUserActionFlushStop
}
AudioPlayerStopReason;

typedef enum
{
	AudioPlayerErrorNone = 0,
	AudioPlayerErrorDataSource,
    AudioPlayerErrorStreamParseBytesFailed,
    AudioPlayerErrorDataNotFound,
    AudioPlayerErrorQueueStartFailed,
    AudioPlayerErrorQueuePauseFailed,
    AudioPlayerErrorUnknownBuffer,
    AudioPlayerErrorQueueStopFailed,
    AudioPlayerErrorOther
}
AudioPlayerErrorCode;

@class AudioPlayer;

@protocol AudioPlayerDelegate <NSObject>
-(void) audioPlayer:(AudioPlayer*)audioPlayer stateChanged:(AudioPlayerState)state;
-(void) audioPlayer:(AudioPlayer*)audioPlayer didEncounterError:(AudioPlayerErrorCode)errorCode;
-(void) audioPlayer:(AudioPlayer*)audioPlayer didStartPlayingQueueItemId:(NSObject*)queueItemId;
-(void) audioPlayer:(AudioPlayer*)audioPlayer didFinishBufferingSourceWithQueueItemId:(NSObject*)queueItemId;
-(void) audioPlayer:(AudioPlayer*)audioPlayer didFinishPlayingQueueItemId:(NSObject*)queueItemId withReason:(AudioPlayerStopReason)stopReason andProgress:(double)progress andDuration:(double)duration;
@optional
-(void) audioPlayer:(AudioPlayer*)audioPlayer internalStateChanged:(AudioPlayerInternalState)state;
-(void) audioPlayer:(AudioPlayer*)audioPlayer didCancelQueuedItems:(NSArray*)queuedItems;
@end

@class QueueEntry;

typedef struct
{
    AudioQueueBufferRef ref;
    int bufferIndex;
}
AudioQueueBufferRefLookupEntry;

@interface AudioPlayer : NSObject<DataSourceDelegate>
{
@private
    UInt8* readBuffer;  // 读取到的音频数据缓冲
    int readBufferSize;
	
    NSOperationQueue* fastApiQueue;
    
    QueueEntry* currentlyPlayingEntry; // 当前正在播放的资源
    QueueEntry* currentlyReadingEntry; // 当前正在加载的资源
    
    NSMutableArray* upcomingQueue; // 未处理的资源队列，里面的单元就是QueueEntry
    NSMutableArray* bufferingQueue; // 缓存中的资源队列
    
    NSMutableArray* bufferedEntries; // 已缓存成功的项目。
    
    AudioQueueBufferRef* audioQueueBuffer; // 音频队列缓冲
    AudioQueueBufferRefLookupEntry* audioQueueBufferLookup;
    unsigned int audioQueueBufferRefLookupCount;
    unsigned int audioQueueBufferCount;
    AudioStreamPacketDescription* packetDescs;  // 音频数据包描述
    bool* bufferUsed; // 缓冲是否已被用
    int numberOfBuffersUsed; // 缓存被使用了的数量
    
    AudioQueueRef audioQueue;  // AudioQueue对象
    AudioStreamBasicDescription currentAudioStreamBasicDescription;  // 当前音频格式描述
    
    NSThread* playbackThread;
    NSRunLoop* playbackThreadRunLoop;
    NSConditionLock* threadFinishedCondLock;
    
    AudioFileStreamID audioFileStream; // 音频输入流ID
    
    BOOL discontinuous;
    
    int bytesFilled; // 已填充的字节数
	int packetsFilled; // 已填充的数据包数
    
    int fillBufferIndex;
    
	UIBackgroundTaskIdentifier backgroundTaskId;
	
    AudioPlayerErrorCode errorCode;
    AudioPlayerStopReason stopReason;
    
    int currentlyPlayingLock;
    pthread_mutex_t playerMutex;
    pthread_mutex_t queueBuffersMutex;
    pthread_cond_t queueBufferReadyCondition;
    
    volatile BOOL waiting;
    volatile BOOL disposeWasRequested;
    volatile BOOL seekToTimeWasRequested;
    volatile BOOL newFileToPlay;
    volatile double requestedSeekTime;
    volatile BOOL audioQueueFlushing;
    volatile SInt64 audioPacketsReadCount; // 已读取的音频包数量
    volatile SInt64 audioPacketsPlayedCount; // 已播放的音频包数量
}

@property (readonly) double duration;
@property (readonly) double progress;
@property (readwrite) AudioPlayerState state;
@property (readonly) AudioPlayerStopReason stopReason;
@property (readwrite, unsafe_unretained) id<AudioPlayerDelegate> delegate;

-(id) init;
-(id) initWithNumberOfAudioQueueBuffers:(int)numberOfAudioQueueBuffers andReadBufferSize:(int)readBufferSizeIn;
-(DataSource*) dataSourceFromURL:(NSURL*)url;
-(void) play:(NSURL*)url;
-(void) queueDataSource:(DataSource*)dataSource withQueueItemId:(NSObject*)queueItemId;
-(void) setDataSource:(DataSource*)dataSourceIn withQueueItemId:(NSObject*)queueItemId;
-(void) seekToTime:(double)value;
-(void) pause;
-(void) resume;
-(void) stop;
-(void) flushStop;
-(void) dispose;
-(NSObject*) currentlyPlayingQueueItemId;

@end
