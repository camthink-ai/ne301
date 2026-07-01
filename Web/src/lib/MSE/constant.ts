/*
 * @Description: Player constant definitions
 */

// Player event types
export const playerEvent = {
    startPlay: 'startPlay',
    stopPlay: 'stopPlay',
    mseError: 'mseError',
    codecChanged: 'codecChanged',
    videoError: 'videoError',
    audioError: 'audioError'
}

// Player states
export const playerStateIdle = 0
export const playerStatePlaying = 1
export const playerStatePaused = 2
export const playerStateError = 3

// Decode source types
export const decodeourceType = {
    video: 'video',
    audio: 'audio'
}

// Frame header size
export const frameHeaderSize = 88

// Special duration
export const specialDuration = 1 // 1 second
export const maxDuration = 10 // 10 seconds

// JMuxer feed `time` is in milliseconds (internally multiplied by 1000 for microseconds)
export const previewFrameMs = 33; // ~30fps
export const maxFrameMs = maxDuration * 1000;

// Debug switches
export const playDebug = false
export const displaySelf = false

// Resolution
export const resolution1080p = 1920 * 1080

// Frame types
export const frameType = {
    IFrame: 1,
    PFrame: 0
}

// Codec types
export const codecType = {
    H264: 0x1B,
    H265: 0x48323635
} 