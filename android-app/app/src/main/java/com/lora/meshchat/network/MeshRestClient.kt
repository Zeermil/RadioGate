package com.lora.meshchat.network

import android.os.Handler
import android.os.Looper
import com.lora.meshchat.data.ConnectSession
import com.lora.meshchat.data.NodeStatus
import com.lora.meshchat.data.RosterEntry
import com.lora.meshchat.data.SendAccepted
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.io.IOException
import java.util.concurrent.TimeUnit

class MeshRestClient {
    data class ApiError(val httpCode: Int, val code: String, val message: String)

    interface Callback<T> {
        fun onSuccess(value: T)
        fun onError(error: ApiError)
    }

    data class ConnectPending(val retryAfterMs: Long)

    private val mainHandler = Handler(Looper.getMainLooper())
    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .writeTimeout(10, TimeUnit.SECONDS)
        .retryOnConnectionFailure(false)
        .build()

    fun connect(
        username: String,
        stableClientId: String,
        mac: String,
        callback: Callback<Any>
    ) {
        val payload = JSONObject()
            .put("username", username)
            .put("clientId", stableClientId)
            .put("mac", mac)
        post("/connect", payload, object : Callback<JSONObject> {
            override fun onSuccess(value: JSONObject) {
                val status = value.optString("status")
                if (status == "pending") {
                    callback.onSuccess(ConnectPending(value.optLong("retryAfterMs", 1000L)))
                    return
                }
                callback.onSuccess(
                    ConnectSession(
                        username = value.getString("username"),
                        nodeId = value.getString("nodeId"),
                        displayName = value.getString("displayName"),
                        sessionToken = value.getString("sessionToken"),
                        sessionTtlSec = value.optLong("sessionTtlSec", 1800L)
                    )
                )
            }

            override fun onError(error: ApiError) = callback.onError(error)
        })
    }

    fun fetchClients(username: String, sessionToken: String, callback: Callback<List<RosterEntry>>) {
        val request = Request.Builder()
            .url(BASE_URL + "/clients")
            .get()
            .header("X-Username", username)
            .header("X-Session-Token", sessionToken)
            .build()
        execute(request, object : Callback<JSONObject> {
            override fun onSuccess(value: JSONObject) {
                callback.onSuccess(parseRoster(value.optJSONArray("clients") ?: JSONArray()))
            }

            override fun onError(error: ApiError) = callback.onError(error)
        })
    }

    fun status(callback: Callback<NodeStatus>) {
        val request = Request.Builder()
            .url(BASE_URL + "/status")
            .get()
            .build()
        execute(request, object : Callback<JSONObject> {
            override fun onSuccess(value: JSONObject) {
                callback.onSuccess(
                    NodeStatus(
                        nodeId = value.optString("nodeId"),
                        nodeHash = value.optString("nodeHash"),
                        efuseMacSuffix = value.optString("efuseMacSuffix"),
                        displayName = value.optString("displayName"),
                        uptimeMs = value.optLong("uptimeMs"),
                        localClients = value.optInt("localClients"),
                        knownClients = value.optInt("knownClients"),
                        remoteClients = value.optInt("remoteClients"),
                        neighbors = value.optInt("neighbors"),
                        routes = value.optInt("routes"),
                        wsClients = value.optInt("wsClients"),
                        frequencyHz = value.optLong("frequencyHz"),
                        bandwidthHz = value.optLong("bandwidthHz"),
                        spreadingFactor = value.optInt("spreadingFactor"),
                        codingRate = value.optInt("codingRate"),
                        txPower = value.optInt("txPower"),
                        syncWord = value.optInt("syncWord"),
                        meshAddress = value.optInt("meshAddress"),
                        freeHeap = value.optLong("freeHeap"),
                        minFreeHeap = value.optLong("minFreeHeap"),
                        freePsram = value.optLong("freePsram"),
                        loraQueue = value.optInt("loraQueue"),
                        meshQueueDepth = value.optInt("meshQueueDepth", value.optInt("loraQueue")),
                        presenceIntervalMs = value.optLong("presenceIntervalMs"),
                        seenPackets = value.optInt("seenPackets"),
                        wsAwaitingPong = value.optInt("wsAwaitingPong"),
                        lastWsPingMs = value.optLong("lastWsPingMs"),
                        lastWsPongMs = value.optLong("lastWsPongMs"),
                        loraTxCount = value.optLong("loraTxCount"),
                        loraRxCount = value.optLong("loraRxCount"),
                        lastLoRaRssi = value.optInt("lastLoRaRssi"),
                        lastLoRaSnr = value.optDouble("lastLoRaSnr"),
                        lastRadioActivityMs = value.optLong("lastRadioActivityMs"),
                        lastRadioError = value.optString("lastRadioError", "none"),
                        lastMeshPacketType = value.optString("lastMeshPacketType"),
                        lastMeshFrom = value.optString("lastMeshFrom"),
                        lastMeshTo = value.optString("lastMeshTo"),
                        lastPresenceUsers = value.optInt("lastPresenceUsers", value.optInt("lastSyncClientCount")),
                        lastPresenceFrom = value.optString("lastPresenceFrom", value.optString("lastSyncFrom")),
                        lastSyncClientCount = value.optInt("lastSyncClientCount"),
                        lastSyncFrom = value.optString("lastSyncFrom"),
                        selfOriginEchoDrops = value.optLong("selfOriginEchoDrops"),
                        firmwareBuildId = value.optString("firmwareBuildId"),
                        transportRevision = value.optString("transportRevision"),
                        lastError = value.optString("lastError", "none")
                    )
                )
            }

            override fun onError(error: ApiError) = callback.onError(error)
        })
    }

    fun sendMessage(
        sessionToken: String,
        to: String,
        clientMessageId: String,
        content: String,
        callback: Callback<SendAccepted>
    ) {
        val payload = JSONObject()
            .put("to", to)
            .put("clientMessageId", clientMessageId)
            .put("content", content)
            .put("sessionToken", sessionToken)
        post("/send", payload, object : Callback<JSONObject> {
            override fun onSuccess(value: JSONObject) {
                callback.onSuccess(
                    SendAccepted(
                        messageId = value.getString("messageId"),
                        deliveryStatus = value.optString("deliveryStatus", "pending")
                    )
                )
            }

            override fun onError(error: ApiError) = callback.onError(error)
        })
    }

    fun disconnect(sessionToken: String, callback: Callback<Unit>) {
        val payload = JSONObject().put("sessionToken", sessionToken)
        post("/disconnect", payload, object : Callback<JSONObject> {
            override fun onSuccess(value: JSONObject) {
                callback.onSuccess(Unit)
            }

            override fun onError(error: ApiError) = callback.onError(error)
        })
    }

    private fun post(path: String, payload: JSONObject, callback: Callback<JSONObject>) {
        val request = Request.Builder()
            .url(BASE_URL + path)
            .post(payload.toString().toRequestBody(JSON_MEDIA))
            .build()
        execute(request, callback)
    }

    private fun execute(request: Request, callback: Callback<JSONObject>) {
        client.newCall(request).enqueue(object : okhttp3.Callback {
            override fun onFailure(call: okhttp3.Call, e: IOException) {
                mainHandler.post {
                    callback.onError(ApiError(-1, "TRANSPORT_ERROR", e.message ?: "Transport error"))
                }
            }

            override fun onResponse(call: okhttp3.Call, response: okhttp3.Response) {
                response.use { raw ->
                    val body = raw.body?.string().orEmpty()
                    mainHandler.post {
                        val json = runCatching { JSONObject(body) }.getOrNull()
                        if (!raw.isSuccessful || json == null) {
                            callback.onError(
                                ApiError(
                                    raw.code,
                                    json?.optString("code").orEmpty().ifBlank { "HTTP_${raw.code}" },
                                    json?.optString("message").orEmpty().ifBlank { "Unexpected server response" }
                                )
                            )
                            return@post
                        }
                        callback.onSuccess(json)
                    }
                }
            }
        })
    }

    private fun parseRoster(array: JSONArray): List<RosterEntry> {
        return buildList {
            for (index in 0 until array.length()) {
                val item = array.getJSONObject(index)
                add(
                    RosterEntry(
                        username = item.getString("username"),
                        homeNode = item.optString("homeNode"),
                        displayNode = item.optString("displayNode"),
                        isOnline = item.optBoolean("isOnline"),
                        isLocal = item.optBoolean("isLocal")
                    )
                )
            }
        }
    }

    private companion object {
        val JSON_MEDIA = "application/json; charset=utf-8".toMediaType()
        const val BASE_URL = "http://192.168.4.1/api"
    }
}
