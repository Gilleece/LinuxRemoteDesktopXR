package com.rldxr.client

import android.util.Log
import java.io.ByteArrayOutputStream

class RtpDepacketizer(private val onNalUnitReady: (ByteArray) -> Unit) {

    private val TAG = "RtpDepacketizer"
    private var fragmentBuffer: ByteArrayOutputStream? = null
    private var currentTimestamp: Long = -1

    fun handleRtpPacket(packet: ByteArray, length: Int) {
        if (length < 12) {
            Log.w(TAG, "RTP packet too short")
            return
        }

        // Parse RTP Header
        val payloadType = packet[1].toInt() and 0x7F
        val sequenceNumber = ((packet[2].toInt() and 0xFF) shl 8) or (packet[3].toInt() and 0xFF)
        val timestamp = ((packet[4].toLong() and 0xFF) shl 24) or
                        ((packet[5].toLong() and 0xFF) shl 16) or
                        ((packet[6].toLong() and 0xFF) shl 8) or
                        (packet[7].toLong() and 0xFF)

        val payload = packet.copyOfRange(12, length)

        // Check for H.264 payload type (dynamic, but often 96)
        if (payloadType != 96) {
            Log.w(TAG, "Ignoring packet with unknown payload type: $payloadType")
            return
        }

        val nalUnitType = payload[0].toInt() and 0x1F
        
        when (nalUnitType) {
            in 1..23 -> {
                // Single NAL Unit Packet
                onNalUnitReady(payload)
            }
            28 -> { // FU-A
                handleFuaPacket(payload, timestamp)
            }
            else -> {
                Log.w(TAG, "Unsupported NAL unit type: $nalUnitType")
            }
        }
    }

    private fun handleFuaPacket(payload: ByteArray, timestamp: Long) {
        if (payload.size < 2) return

        val fuHeader = payload[1].toInt()
        val isStart = (fuHeader and 0x80) != 0
        val isEnd = (fuHeader and 0x40) != 0
        val nalType = fuHeader and 0x1F

        if (isStart) {
            fragmentBuffer = ByteArrayOutputStream()
            // Reconstruct the original NAL header
            val nalHeader = (payload[0].toInt() and 0xE0) or nalType
            fragmentBuffer?.write(nalHeader)
            currentTimestamp = timestamp
        }

        if (fragmentBuffer != null && timestamp == currentTimestamp) {
            fragmentBuffer?.write(payload, 2, payload.size - 2)
        } else if (fragmentBuffer != null) {
            Log.w(TAG, "Dropping FU-A packet with mismatched timestamp")
            // Reset on timestamp mismatch
            fragmentBuffer = null
        }

        if (isEnd && fragmentBuffer != null) {
            onNalUnitReady(fragmentBuffer!!.toByteArray())
            fragmentBuffer = null
        }
    }
}
