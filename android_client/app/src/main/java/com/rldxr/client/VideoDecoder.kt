package com.rldxr.client

import android.media.MediaCodec
import android.util.Log

class VideoDecoder(private val mediaCodec: MediaCodec) {

    private val TAG = "VideoDecoder"

    fun onNalUnitReceived(nalUnit: ByteArray) {
        try {
            val inputBufferIndex = mediaCodec.dequeueInputBuffer(10000) // 10ms timeout
            if (inputBufferIndex >= 0) {
                val inputBuffer = mediaCodec.getInputBuffer(inputBufferIndex)
                inputBuffer?.put(nalUnit)
                mediaCodec.queueInputBuffer(inputBufferIndex, 0, nalUnit.size, 0, 0)
            }

            val bufferInfo = MediaCodec.BufferInfo()
            var outputBufferIndex = mediaCodec.dequeueOutputBuffer(bufferInfo, 10000)
            while (outputBufferIndex >= 0) {
                mediaCodec.releaseOutputBuffer(outputBufferIndex, true)
                outputBufferIndex = mediaCodec.dequeueOutputBuffer(bufferInfo, 0)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error decoding NAL unit", e)
        }
    }
}
