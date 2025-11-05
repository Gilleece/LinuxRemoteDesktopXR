package com.rldxr.client

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Bundle
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.Button
import android.widget.EditText
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress

class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private lateinit var ipAddressEditText: EditText
    private lateinit var connectButton: Button
    private lateinit var surfaceView: SurfaceView
    private lateinit var surfaceHolder: SurfaceHolder
    private var mediaCodec: MediaCodec? = null
    private val udpPort = 4242
    private val TAG = "RLDXRClient"
    private lateinit var videoDecoder: VideoDecoder
    private lateinit var rtpDepacketizer: RtpDepacketizer
    private val udpScope = CoroutineScope(Dispatchers.IO)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        surfaceView = findViewById(R.id.surfaceView)
        connectButton = findViewById(R.id.connectButton)
        ipAddressEditText = findViewById(R.id.ipAddressEditText)
        surfaceHolder = surfaceView.holder
        surfaceHolder.addCallback(this)

        connectButton.setOnClickListener {
            val ipAddress = ipAddressEditText.text.toString()
            if (ipAddress.isNotEmpty()) {
                Log.d("MainActivity", "Connect button clicked. IP: $ipAddress")
                Toast.makeText(this, "Connecting to $ipAddress...", Toast.LENGTH_SHORT).show()
                startUdpListener(ipAddress)
            } else {
                Toast.makeText(this, "Please enter an IP address.", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun startUdpListener(ipAddress: String) {
        udpScope.launch(Dispatchers.IO) {
            try {
                Log.d("MainActivity", "Starting UDP listener on port 5004")
                val socket = DatagramSocket(5004)
                val buffer = ByteArray(2048)
                val packet = DatagramPacket(buffer, buffer.size)

                // Send a dummy packet to establish connection to the server
                val serverAddr = InetAddress.getByName(ipAddress)
                val dummyData = "hello".toByteArray()
                val dummyPacket = DatagramPacket(dummyData, dummyData.size, serverAddr, 5004)
                socket.send(dummyPacket)
                Log.d("MainActivity", "Sent initial packet to $ipAddress:5004")

                while (isActive) {
                    try {
                        socket.receive(packet)
                        rtpDepacketizer.handleRtpPacket(packet.data, packet.length)
                    } catch (e: Exception) {
                        Log.e("MainActivity", "Error processing packet", e)
                    }
                }
                socket.close()
                Log.d("MainActivity", "UDP listener stopped.")
            } catch (e: Exception) {
                Log.e("MainActivity", "UDP Listener failed", e)
            }
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        Log.d(TAG, "Surface created")
        try {
            val format = MediaFormat.createVideoFormat("video/avc", 1920, 1080)
            mediaCodec = MediaCodec.createDecoderByType("video/avc")
            mediaCodec?.let { 
                videoDecoder = VideoDecoder(it)
                rtpDepacketizer = RtpDepacketizer { nalUnit ->
                    videoDecoder.onNalUnitReceived(nalUnit)
                }
                it.configure(format, holder.surface, null, 0)
                it.start()
                Log.d(TAG, "MediaCodec decoder started")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error setting up MediaCodec", e)
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        // Not used
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Log.d(TAG, "Surface destroyed")
        mediaCodec?.stop()
        mediaCodec?.release()
        mediaCodec = null
    }
}
