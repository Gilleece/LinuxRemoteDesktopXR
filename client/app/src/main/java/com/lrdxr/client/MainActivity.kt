package com.lrdxr.client

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import android.widget.Button
import android.widget.EditText
import android.widget.RadioGroup
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import org.java_websocket.client.WebSocketClient
import org.java_websocket.handshake.ServerHandshake
import org.json.JSONObject
import org.webrtc.*
import java.net.URI
import java.nio.ByteBuffer
import java.util.*

class MainActivity : AppCompatActivity() {

    private val TAG = "WebRTC_Client"
    
    private lateinit var surfaceView: SurfaceViewRenderer
    private lateinit var cursorView: View
    private lateinit var ipInput: EditText
    private lateinit var connectButton: Button
    private lateinit var disconnectButton: Button
    private lateinit var inputLayout: View
    private lateinit var resolutionGroup: RadioGroup
    
    private lateinit var peerConnectionFactory: PeerConnectionFactory
    private lateinit var peerConnection: PeerConnection
    private var eglBase: EglBase? = null
    private var webSocketClient: WebSocketClient? = null
    
    private val hideUiHandler = android.os.Handler(android.os.Looper.getMainLooper())
    private val hideUiRunnable = Runnable {
        inputLayout.animate().alpha(0f).setDuration(500).withEndAction {
            inputLayout.visibility = View.GONE
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        surfaceView = findViewById(R.id.surface_view)
        cursorView = findViewById(R.id.cursor_view)
        ipInput = findViewById(R.id.ip_input)
        connectButton = findViewById(R.id.connect_button)
        disconnectButton = findViewById(R.id.disconnect_button)
        inputLayout = findViewById(R.id.input_layout)
        resolutionGroup = findViewById(R.id.resolution_group)
        
        connectButton.setOnClickListener {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO), 1)
            } else {
                startWebRTC()
            }
        }
        
        disconnectButton.setOnClickListener {
            disconnect()
        }
        
        surfaceView.setOnClickListener {
            showUi()
        }
    }
    
    private fun showUi() {
        inputLayout.visibility = View.VISIBLE
        inputLayout.alpha = 1f
        resetHideTimer()
    }
    
    private fun resetHideTimer() {
        hideUiHandler.removeCallbacks(hideUiRunnable)
        if (disconnectButton.visibility == View.VISIBLE) {
             hideUiHandler.postDelayed(hideUiRunnable, 3000)
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 1 && grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            startWebRTC()
        } else {
            Toast.makeText(this, "Permissions required for WebRTC", Toast.LENGTH_LONG).show()
        }
    }

    private fun startWebRTC() {
        try {
            connectButton.visibility = View.GONE
            disconnectButton.visibility = View.VISIBLE
            ipInput.isEnabled = false
            resolutionGroup.visibility = View.GONE
            resetHideTimer()
            
            // Always re-init surface view and peer connection
            initWebRTC()
            connectSignaling()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start WebRTC", e)
            runOnUiThread {
                Toast.makeText(this, "Error: ${e.message}", Toast.LENGTH_LONG).show()
                disconnect()
            }
        }
    }
    
    private fun disconnect() {
        try {
            webSocketClient?.close()
            peerConnection.close()
            peerConnectionFactory.dispose()
            eglBase?.release()
            eglBase = null
            
            runOnUiThread {
                connectButton.visibility = View.VISIBLE
                disconnectButton.visibility = View.GONE
                ipInput.isEnabled = true
                resolutionGroup.visibility = View.VISIBLE
                cursorView.visibility = View.GONE
                surfaceView.clearImage()
                surfaceView.release()
                showUi()
                hideUiHandler.removeCallbacks(hideUiRunnable)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error disconnecting", e)
        }
    }

    private fun initWebRTC() {
        eglBase = EglBase.create()
        
        surfaceView.init(eglBase?.eglBaseContext, null)
        surfaceView.setMirror(false)
        surfaceView.setEnableHardwareScaler(true)

        val options = PeerConnectionFactory.InitializationOptions.builder(this)
            .setEnableInternalTracer(true)
            .createInitializationOptions()
        PeerConnectionFactory.initialize(options)
        
        val videoEncoderFactory = DefaultVideoEncoderFactory(eglBase?.eglBaseContext, true, true)
        val videoDecoderFactory = DefaultVideoDecoderFactory(eglBase?.eglBaseContext)

        peerConnectionFactory = PeerConnectionFactory.builder()
            .setVideoEncoderFactory(videoEncoderFactory)
            .setVideoDecoderFactory(videoDecoderFactory)
            .createPeerConnectionFactory()
            
        val rtcConfig = PeerConnection.RTCConfiguration(
            listOf(PeerConnection.IceServer.builder("stun:stun.l.google.com:19302").createIceServer())
        )
        
        peerConnection = peerConnectionFactory.createPeerConnection(rtcConfig, object : PeerConnection.Observer {
            override fun onSignalingChange(state: PeerConnection.SignalingState?) { Log.d(TAG, "Signaling State: $state") }
            override fun onIceConnectionChange(state: PeerConnection.IceConnectionState?) { 
                Log.d(TAG, "ICE Connection State: $state") 
                if (state == PeerConnection.IceConnectionState.CONNECTED) {
                    runOnUiThread { 
                        Toast.makeText(this@MainActivity, "ICE Connected!", Toast.LENGTH_SHORT).show()
                        resetHideTimer()
                    }
                } else if (state == PeerConnection.IceConnectionState.DISCONNECTED || state == PeerConnection.IceConnectionState.FAILED) {
                    runOnUiThread {
                        Toast.makeText(this@MainActivity, "ICE Disconnected", Toast.LENGTH_SHORT).show()
                        disconnect()
                    }
                }
            }
            override fun onIceConnectionReceivingChange(b: Boolean) {}
            override fun onIceGatheringChange(state: PeerConnection.IceGatheringState?) {}
            override fun onIceCandidate(candidate: IceCandidate?) {
                if (candidate != null) {
                    sendIceCandidate(candidate)
                }
            }
            override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>?) {}
            override fun onAddStream(stream: MediaStream?) {
                Log.d(TAG, "Stream added")
                if (stream?.videoTracks?.isNotEmpty() == true) {
                    val track = stream.videoTracks[0]
                    track.addSink(surfaceView)
                }
            }
            override fun onRemoveStream(stream: MediaStream?) {}
            override fun onDataChannel(channel: DataChannel?) {
                Log.d(TAG, "Data Channel received: ${channel?.label()}")
                channel?.registerObserver(object : DataChannel.Observer {
                    override fun onBufferedAmountChange(l: Long) {}
                    override fun onStateChange() { Log.d(TAG, "Data Channel State: ${channel.state()}") }
                    override fun onMessage(buffer: DataChannel.Buffer?) {
                        if (buffer != null && !buffer.binary) return // We expect binary
                        val data = buffer?.data ?: return
                        if (data.remaining() == 8) {
                            val normX = data.getFloat()
                            val normY = data.getFloat()
                            runOnUiThread {
                                val viewWidth = surfaceView.width
                                val viewHeight = surfaceView.height
                                
                                cursorView.x = normX * viewWidth
                                cursorView.y = normY * viewHeight
                                
                                if (cursorView.visibility != View.VISIBLE) {
                                    cursorView.visibility = View.VISIBLE
                                }
                            }
                        }
                    }
                })
            }
            override fun onRenegotiationNeeded() {}
            override fun onAddTrack(receiver: RtpReceiver?, streams: Array<out MediaStream>?) {}
        })!!
    }

    private fun connectSignaling() {
        val ip = ipInput.text.toString()
        val url = "ws://$ip:8080/ws"
        Log.d(TAG, "Connecting to $url")
        runOnUiThread { Toast.makeText(this, "Connecting to $url", Toast.LENGTH_SHORT).show() }
        
        val uri = URI(url)
        webSocketClient = object : WebSocketClient(uri) {
            override fun onOpen(handshakedata: ServerHandshake?) {
                Log.d(TAG, "WebSocket Connected")
                runOnUiThread { Toast.makeText(this@MainActivity, "WebSocket Connected", Toast.LENGTH_SHORT).show() }
                
                val selectedId = resolutionGroup.checkedRadioButtonId
                var width = 1920
                var height = 1080
                
                when (selectedId) {
                    R.id.res_1080p -> { width = 1920; height = 1080 }
                    R.id.res_1440p -> { width = 2560; height = 1440 }
                    R.id.res_1440p_uw -> { width = 3440; height = 1440 }
                    R.id.res_4k -> { width = 3840; height = 2160 }
                }
                
                val json = JSONObject()
                json.put("type", "register")
                json.put("role", "client")
                json.put("width", width)
                json.put("height", height)
                
                send(json.toString())
            }

            override fun onMessage(message: String?) {
                Log.d(TAG, "Received: $message")
                handleMessage(message)
            }

            override fun onClose(code: Int, reason: String?, remote: Boolean) {
                Log.d(TAG, "WebSocket Closed: $reason")
                runOnUiThread { Toast.makeText(this@MainActivity, "WebSocket Closed: $reason", Toast.LENGTH_LONG).show() }
            }

            override fun onError(ex: Exception?) {
                Log.e(TAG, "WebSocket Error", ex)
                runOnUiThread { Toast.makeText(this@MainActivity, "WebSocket Error: ${ex?.message}", Toast.LENGTH_LONG).show() }
            }
        }
        webSocketClient?.connect()
    }

    private fun handleMessage(message: String?) {
        message ?: return
        try {
            val json = JSONObject(message)
            val type = json.getString("type")

            if (type == "offer") {
                val sdp = json.getString("sdp")
                peerConnection.setRemoteDescription(SimpleSdpObserver(), SessionDescription(SessionDescription.Type.OFFER, sdp))
                peerConnection.createAnswer(object : SimpleSdpObserver() {
                    override fun onCreateSuccess(desc: SessionDescription?) {
                        peerConnection.setLocalDescription(SimpleSdpObserver(), desc)
                        val answerJson = JSONObject()
                        answerJson.put("type", "answer")
                        answerJson.put("sdp", desc?.description)
                        send(answerJson.toString())
                    }
                }, MediaConstraints())
            } else if (type == "ice-candidate") {
                val candidate = IceCandidate(
                    json.getString("sdpMid"),
                    json.getInt("sdpMLineIndex"),
                    json.getString("candidate")
                )
                peerConnection.addIceCandidate(candidate)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error handling message", e)
        }
    }

    private fun send(message: String) {
        webSocketClient?.send(message)
    }

    private fun sendIceCandidate(candidate: IceCandidate) {
        val json = JSONObject()
        json.put("type", "ice-candidate")
        json.put("sdpMid", candidate.sdpMid)
        json.put("sdpMLineIndex", candidate.sdpMLineIndex)
        json.put("candidate", candidate.sdp)
        send(json.toString())
    }

    open inner class SimpleSdpObserver : SdpObserver {
        override fun onCreateSuccess(desc: SessionDescription?) {}
        override fun onSetSuccess() {}
        override fun onCreateFailure(s: String?) { Log.e("WebRTC_Client", "SDP Create Failure: $s") }
        override fun onSetFailure(s: String?) { Log.e("WebRTC_Client", "SDP Set Failure: $s") }
    }
}