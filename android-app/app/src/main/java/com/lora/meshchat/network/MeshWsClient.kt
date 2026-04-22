package com.lora.meshchat.network

import android.os.Handler
import android.os.Looper
import android.util.Log
import com.lora.meshchat.data.NodeEvent
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.concurrent.TimeUnit

class MeshWsClient(
    private val listener: Listener
) {
    interface Listener {
        fun onSocketOpen(attemptId: Long)
        fun onSocketEvent(attemptId: Long, event: NodeEvent)
        fun onSocketClosed(attemptId: Long, reason: String)
        fun onSocketFailure(attemptId: Long, message: String)
    }

    private val mainHandler = Handler(Looper.getMainLooper())
    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.MILLISECONDS)
        .pingInterval(20, TimeUnit.SECONDS)
        .retryOnConnectionFailure(false)
        .build()

    private var webSocket: WebSocket? = null
    private var activeAttemptId: Long = 0L

    fun connect(username: String, token: String, attemptId: Long) {
        disconnect("attempt-replace")
        activeAttemptId = attemptId
        Log.d(TAG, "ws-connect attempt=$attemptId user=$username token=${token.take(8)}")

        lateinit var socketRef: WebSocket
        val request = Request.Builder()
            .url("ws://192.168.4.1:81/ws?username=$username&token=$token")
            .build()

        socketRef = client.newWebSocket(request, object : WebSocketListener() {
            private fun isActive(socket: WebSocket): Boolean {
                return activeAttemptId == attemptId && webSocket === socket
            }

            override fun onOpen(webSocket: WebSocket, response: Response) {
                if (!isActive(webSocket)) {
                    Log.d(TAG, "ws-open ignored attempt=$attemptId")
                    return
                }
                Log.d(TAG, "ws-open attempt=$attemptId token=${token.take(8)}")
                mainHandler.post {
                    if (isActive(webSocket)) {
                        listener.onSocketOpen(attemptId)
                    }
                }
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                if (!isActive(webSocket)) {
                    return
                }
                val json = runCatching { JSONObject(text) }.getOrNull() ?: return
                val type = json.optString("type")
                if (type == "PING") {
                    val pong = JSONObject().put("type", "PONG").put("t", json.optLong("t"))
                    webSocket.send(pong.toString())
                    return
                }
                val event = when (type) {
                    "MESSAGE_INCOMING" -> NodeEvent.Message(
                        id = System.currentTimeMillis(),
                        messageId = json.optString("messageId"),
                        from = json.optString("from"),
                        text = json.optString("content"),
                        timestampMs = json.optLong("timestamp", System.currentTimeMillis())
                    )

                    "MESSAGE_STATUS" -> NodeEvent.MessageStatus(
                        id = System.currentTimeMillis(),
                        messageId = json.optString("messageId"),
                        status = json.optString("status"),
                        reason = json.optString("reason").takeIf { it.isNotBlank() }
                    )

                    "NETWORK_EVENT" -> NodeEvent.Network(
                        id = System.currentTimeMillis(),
                        event = json.optString("event"),
                        username = json.optString("username"),
                        homeNode = json.optString("homeNode").takeIf { it.isNotBlank() },
                        displayNode = json.optString("displayNode").takeIf { it.isNotBlank() },
                        online = json.takeIf { it.has("online") }?.optBoolean("online")
                    )

                    else -> NodeEvent.System(
                        id = System.currentTimeMillis(),
                        text = text
                    )
                }
                mainHandler.post {
                    if (isActive(webSocket)) {
                        listener.onSocketEvent(attemptId, event)
                    }
                }
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                if (!isActive(webSocket)) {
                    return
                }
                Log.d(TAG, "ws-closed attempt=$attemptId code=$code reason=$reason")
                this@MeshWsClient.webSocket = null
                activeAttemptId = 0L
                mainHandler.post { listener.onSocketClosed(attemptId, reason.ifBlank { "Closed" }) }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (!isActive(webSocket)) {
                    return
                }
                Log.d(TAG, "ws-failure attempt=$attemptId message=${t.message.orEmpty()}")
                this@MeshWsClient.webSocket = null
                activeAttemptId = 0L
                mainHandler.post { listener.onSocketFailure(attemptId, t.message ?: "WebSocket failure") }
            }
        })
        webSocket = socketRef
    }

    fun sendMessageAck(messageId: String) {
        val ack = JSONObject()
            .put("type", "MESSAGE_ACK")
            .put("messageId", messageId)
            .put("status", "received")
        webSocket?.send(ack.toString())
    }

    fun disconnect(reason: String = "client-close") {
        activeAttemptId = 0L
        val socket = webSocket
        webSocket = null
        Log.d(TAG, "ws-disconnect reason=$reason")
        socket?.close(1000, reason)
    }

    private companion object {
        const val TAG = "MeshWsClient"
    }
}
