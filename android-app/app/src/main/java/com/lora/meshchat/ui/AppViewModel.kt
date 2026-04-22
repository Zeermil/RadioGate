package com.lora.meshchat.ui

import android.app.Application
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import com.lora.meshchat.data.ChatMessage
import com.lora.meshchat.data.ConnectSession
import com.lora.meshchat.data.ConnectionState
import com.lora.meshchat.data.DeliveryState
import com.lora.meshchat.data.NodeEvent
import com.lora.meshchat.data.NodeStatus
import com.lora.meshchat.data.RosterEntry
import com.lora.meshchat.data.SelfNodeInfo
import com.lora.meshchat.data.SystemNotice
import com.lora.meshchat.data.UserPreferencesStore
import com.lora.meshchat.network.MeshRestClient
import com.lora.meshchat.network.MeshWsClient
import java.util.UUID

data class AppUiState(
    val connectionState: ConnectionState = ConnectionState.Disconnected,
    val usernameInput: String = "",
    val transportAttemptId: Long = 0,
    val selfNode: SelfNodeInfo? = null,
    val nodeStatus: NodeStatus = NodeStatus(),
    val roster: List<RosterEntry> = emptyList(),
    val selectedPeer: RosterEntry? = null,
    val draftMessage: String = "",
    val lastError: String? = null,
    val connectionMessage: String = "",
    val notices: List<SystemNotice> = emptyList(),
    val sendInProgress: Boolean = false
)

class AppViewModel(application: Application) : AndroidViewModel(application), MeshWsClient.Listener {
    private val prefs = UserPreferencesStore(application.applicationContext)
    private val restClient = MeshRestClient()
    private val wsClient = MeshWsClient(this)
    private val mainHandler = Handler(Looper.getMainLooper())
    private val conversations = mutableStateMapOf<String, List<ChatMessage>>()

    private var manualDisconnect = false
    private var pendingConnectRunnable: Runnable? = null
    private var pendingRosterRefreshRunnable: Runnable? = null
    private var activeAttemptId = 0L
    private var nextAttemptId = 1L

    var uiState by mutableStateOf(AppUiState(usernameInput = prefs.lastUsername()))
        private set

    val selectedMessages: List<ChatMessage>
        get() = uiState.selectedPeer?.let { conversations[it.key].orEmpty() } ?: emptyList()

    init {
        val savedUsername = prefs.lastUsername()
        if (savedUsername.isNotBlank()) {
            connectInternal(savedUsername, automatic = true)
        }
    }

    fun onUsernameChanged(value: String) {
        uiState = uiState.copy(usernameInput = value, lastError = null)
    }

    fun connect() {
        if (uiState.connectionState in setOf(ConnectionState.Connecting, ConnectionState.CheckingNickname, ConnectionState.Reconnecting, ConnectionState.Leaving)) {
            uiState = uiState.copy(lastError = "Wait for the current connection attempt")
            return
        }
        val username = uiState.usernameInput.trim()
        if (username.isBlank()) {
            uiState = uiState.copy(lastError = "Enter a nickname")
            return
        }
        connectInternal(username, automatic = false)
    }

    fun retryConnection() {
        reconnectTransport()
    }

    fun reconnectTransport() {
        val username = uiState.selfNode?.username.orEmpty()
            .ifBlank { uiState.usernameInput.trim() }
            .ifBlank { prefs.lastUsername() }
        if (username.isBlank()) {
            uiState = uiState.copy(lastError = "Enter a nickname")
            return
        }
        if (uiState.connectionState in setOf(ConnectionState.Connecting, ConnectionState.CheckingNickname, ConnectionState.Leaving)) {
            return
        }
        connectInternal(username, automatic = false)
    }

    fun refreshUsers() {
        cancelPendingRosterRefresh()
        refreshRoster()
    }

    fun refreshNode() {
        refreshStatus()
    }

    fun leave() {
        manualDisconnect = true
        cancelPendingConnect("leave")
        cancelPendingRosterRefresh()
        activeAttemptId = 0L
        val token = uiState.selfNode?.sessionToken
        wsClient.disconnect("client-close")
        if (token.isNullOrBlank()) {
            resetSessionState()
            return
        }
        uiState = uiState.copy(connectionState = ConnectionState.Leaving, connectionMessage = "Closing session")
        restClient.disconnect(token, object : MeshRestClient.Callback<Unit> {
            override fun onSuccess(value: Unit) = resetSessionState()
            override fun onError(error: MeshRestClient.ApiError) = resetSessionState()
        })
    }

    fun openChat(entry: RosterEntry) {
        uiState = uiState.copy(selectedPeer = entry, draftMessage = "", lastError = null)
    }

    fun backToRoster() {
        uiState = uiState.copy(selectedPeer = null, draftMessage = "", sendInProgress = false)
    }

    fun onDraftChanged(value: String) {
        uiState = uiState.copy(draftMessage = value)
    }

    fun sendDraft() {
        val selfNode = uiState.selfNode ?: return
        val peer = uiState.selectedPeer ?: return
        val text = uiState.draftMessage.trim()
        if (text.isBlank() || uiState.sendInProgress) {
            return
        }

        val localId = UUID.randomUUID().toString()
        appendMessage(
            peer.key,
            ChatMessage(
                localId = localId,
                peerKey = peer.key,
                fromSelf = true,
                text = text,
                timestampMs = System.currentTimeMillis(),
                deliveryState = DeliveryState.Sending
            )
        )
        uiState = uiState.copy(draftMessage = "", sendInProgress = true, lastError = null)

        val sessionToken = selfNode.sessionToken
        restClient.sendMessage(sessionToken, peer.username, localId, text, object : MeshRestClient.Callback<com.lora.meshchat.data.SendAccepted> {
            override fun onSuccess(value: com.lora.meshchat.data.SendAccepted) {
                updateMessages { message ->
                    if (message.localId == localId) {
                        message.copy(remoteMessageId = value.messageId, deliveryState = DeliveryState.Accepted)
                    } else {
                        message
                    }
                }
                uiState = uiState.copy(sendInProgress = false)
            }

            override fun onError(error: MeshRestClient.ApiError) {
                updateMessages { message ->
                    if (message.localId == localId) {
                        message.copy(deliveryState = DeliveryState.Failed)
                    } else {
                        message
                    }
                }
                uiState = uiState.copy(sendInProgress = false)
                if (uiState.selfNode?.sessionToken != sessionToken) {
                    logTransport("connect-stale-ignored", activeAttemptId, "send-response token-changed")
                    return
                }
                if (handleApiError(error, "Session expired while sending. Reconnecting")) {
                    return
                }
                if (error.code == "USER_OFFLINE") {
                    markPeerOffline(peer.username)
                    scheduleRosterRefresh(150L)
                    addNotice("${peer.username} is offline")
                }
                uiState = uiState.copy(lastError = error.message)
                if (error.code != "USER_OFFLINE") {
                    addNotice("Send failed: ${error.message}")
                }
            }
        })
    }

    override fun onSocketOpen(attemptId: Long) {
        if (ignoreStaleAttempt(attemptId, "ws-open")) {
            return
        }
        logTransport("ws-open", attemptId, "token=${uiState.selfNode?.sessionToken?.take(8).orEmpty()}")
        uiState = uiState.copy(
            connectionState = ConnectionState.Connected,
            connectionMessage = "WebSocket connected",
            lastError = null
        )
        refreshStatus()
        scheduleRosterRefresh(150L)
    }

    override fun onSocketEvent(attemptId: Long, event: NodeEvent) {
        if (ignoreStaleAttempt(attemptId, "ws-event")) {
            return
        }
        when (event) {
            is NodeEvent.Message -> {
                appendMessage(
                    event.from,
                    ChatMessage(
                        localId = "in-${event.messageId}",
                        peerKey = event.from,
                        fromSelf = false,
                        text = event.text,
                        timestampMs = event.timestampMs,
                        deliveryState = DeliveryState.Delivered,
                        remoteMessageId = event.messageId
                    )
                )
                wsClient.sendMessageAck(event.messageId)
            }

            is NodeEvent.MessageStatus -> {
                val state = when (event.status.lowercase()) {
                    "delivered" -> DeliveryState.Delivered
                    "accepted", "pending" -> DeliveryState.Accepted
                    else -> DeliveryState.Failed
                }
                updateMessages { message ->
                    if (message.remoteMessageId == event.messageId) {
                        message.copy(deliveryState = state)
                    } else {
                        message
                    }
                }
                if (state == DeliveryState.Failed) {
                    uiState = uiState.copy(lastError = event.reason ?: "Delivery failed")
                }
            }

            is NodeEvent.Network -> {
                addNotice("${event.username}: ${event.event}")
                applyNetworkEvent(event)
                scheduleRosterRefresh()
            }

            is NodeEvent.System -> addNotice(event.text)
        }
    }

    override fun onSocketClosed(attemptId: Long, reason: String) {
        if (ignoreStaleAttempt(attemptId, "ws-closed")) {
            return
        }
        logTransport("ws-closed", attemptId, reason)
        if (manualDisconnect) {
            return
        }
        uiState = uiState.copy(
            connectionState = ConnectionState.Reconnecting,
            connectionMessage = reason,
            lastError = reason
        )
        scheduleReconnect(attemptId, reason)
    }

    override fun onSocketFailure(attemptId: Long, message: String) {
        if (ignoreStaleAttempt(attemptId, "ws-failure")) {
            return
        }
        logTransport("ws-failure", attemptId, message)
        if (manualDisconnect) {
            return
        }
        uiState = uiState.copy(
            connectionState = ConnectionState.Offline,
            connectionMessage = message,
            lastError = message
        )
        scheduleReconnect(attemptId, message)
    }

    override fun onCleared() {
        super.onCleared()
        cancelPendingConnect("clear")
        cancelPendingRosterRefresh()
        activeAttemptId = 0L
        wsClient.disconnect("viewmodel-cleared")
    }

    private fun connectInternal(username: String, automatic: Boolean) {
        val attemptId = nextAttemptId++
        manualDisconnect = false
        cancelPendingConnect("connect-start")
        cancelPendingRosterRefresh()
        activeAttemptId = attemptId
        prefs.saveUsername(username)
        wsClient.disconnect("attempt-$attemptId")
        uiState = uiState.copy(
            usernameInput = username,
            transportAttemptId = attemptId,
            connectionState = if (automatic) ConnectionState.Reconnecting else ConnectionState.Connecting,
            connectionMessage = if (automatic) "Restoring session" else "Connecting to node",
            lastError = null
        )
        logTransport("connect-start", attemptId, "auto=$automatic user=$username")

        restClient.connect(username, prefs.stableClientId(), prefs.pseudoMac(), object : MeshRestClient.Callback<Any> {
            override fun onSuccess(value: Any) {
                if (ignoreStaleAttempt(attemptId, "connect-response")) {
                    return
                }
                when (value) {
                    is MeshRestClient.ConnectPending -> {
                        uiState = uiState.copy(
                            connectionState = ConnectionState.CheckingNickname,
                            connectionMessage = "Checking nickname across mesh"
                        )
                        scheduleRetry(username, value.retryAfterMs, attemptId, "claim-pending")
                    }

                    is ConnectSession -> onConnectedSession(attemptId, value)
                }
            }

            override fun onError(error: MeshRestClient.ApiError) {
                if (ignoreStaleAttempt(attemptId, "connect-error")) {
                    return
                }
                logTransport("connect-error", attemptId, "${error.code}:${error.message}")
                uiState = uiState.copy(
                    connectionState = if (automatic) ConnectionState.Offline else ConnectionState.Disconnected,
                    connectionMessage = error.message,
                    lastError = error.message
                )
                if (automatic) {
                    scheduleReconnect(attemptId, error.message)
                }
            }
        })
    }

    private fun onConnectedSession(attemptId: Long, session: ConnectSession) {
        if (ignoreStaleAttempt(attemptId, "connect-success")) {
            return
        }
        prefs.saveSession(session.sessionToken, session.nodeId, session.displayName)
        uiState = uiState.copy(
            selfNode = SelfNodeInfo(
                username = session.username,
                stableClientId = prefs.stableClientId(),
                nodeId = session.nodeId,
                displayName = session.displayName,
                sessionToken = session.sessionToken
            ),
            connectionState = ConnectionState.Connecting,
            connectionMessage = "Opening WebSocket",
            lastError = null
        )
        logTransport("connect-success", attemptId, "node=${session.nodeId} token=${session.sessionToken.take(8)}")
        wsClient.connect(session.username, session.sessionToken, attemptId)
    }

    private fun refreshRoster() {
        val selfNode = uiState.selfNode ?: run {
            uiState = uiState.copy(lastError = "Connect first to refresh users")
            return
        }
        val attemptId = activeAttemptId
        val sessionToken = selfNode.sessionToken
        restClient.fetchClients(selfNode.username, sessionToken, object : MeshRestClient.Callback<List<RosterEntry>> {
            override fun onSuccess(value: List<RosterEntry>) {
                if (!isCurrentSession(attemptId, sessionToken)) {
                    logTransport("connect-stale-ignored", attemptId, "refresh-users-success")
                    return
                }
                applyRoster(value)
                uiState = uiState.copy(lastError = null)
            }

            override fun onError(error: MeshRestClient.ApiError) {
                if (!isCurrentSession(attemptId, sessionToken)) {
                    logTransport("connect-stale-ignored", attemptId, "refresh-users-error")
                    return
                }
                if (handleApiError(error, "Session expired. Reconnecting")) {
                    return
                }
                uiState = uiState.copy(lastError = error.message)
            }
        })
    }

    private fun refreshStatus() {
        restClient.status(object : MeshRestClient.Callback<NodeStatus> {
            override fun onSuccess(value: NodeStatus) {
                uiState = uiState.copy(nodeStatus = value)
            }

            override fun onError(error: MeshRestClient.ApiError) {
                uiState = uiState.copy(lastError = error.message)
            }
        })
    }

    private fun applyRoster(entries: List<RosterEntry>) {
        val selfUsername = uiState.selfNode?.username
        val filtered = entries
            .filterNot { it.username == selfUsername }
            .sortedWith(compareBy<RosterEntry> { !it.isLocal }.thenBy { it.username.lowercase() })
        uiState = uiState.copy(
            roster = filtered,
            selectedPeer = uiState.selectedPeer?.let { selected -> filtered.firstOrNull { it.key == selected.key } }
        )
    }

    private fun applyNetworkEvent(event: NodeEvent.Network) {
        val selfNode = uiState.selfNode ?: return
        if (event.username == selfNode.username) {
            return
        }

        val current = uiState.roster.toMutableList()
        val existingIndex = current.indexOfFirst { it.username == event.username }
        val existing = current.getOrNull(existingIndex)
        val online = event.online ?: event.event.equals("USER_JOINED", ignoreCase = true)
        val homeNode = event.homeNode.orEmpty().ifBlank { existing?.homeNode.orEmpty() }
        val displayNode = event.displayNode.orEmpty().ifBlank { existing?.displayNode.orEmpty() }
        val isLocal = if (homeNode.isNotBlank()) homeNode == selfNode.nodeId else existing?.isLocal ?: false

        if (existingIndex >= 0) {
            current[existingIndex] = existing!!.copy(
                homeNode = if (homeNode.isNotBlank()) homeNode else existing.homeNode,
                displayNode = if (displayNode.isNotBlank()) displayNode else existing.displayNode,
                isOnline = online,
                isLocal = isLocal
            )
        } else if (online) {
            current.add(
                RosterEntry(
                    username = event.username,
                    homeNode = homeNode,
                    displayNode = displayNode.ifBlank { homeNode },
                    isOnline = true,
                    isLocal = isLocal
                )
            )
        }

        applyRoster(current)
    }

    private fun markPeerOffline(username: String) {
        val updated = uiState.roster.map { entry ->
            if (entry.username == username) entry.copy(isOnline = false) else entry
        }
        applyRoster(updated)
    }

    private fun scheduleRosterRefresh(delayMs: Long = 350L) {
        val selfNode = uiState.selfNode ?: return
        val attemptId = activeAttemptId
        val sessionToken = selfNode.sessionToken
        cancelPendingRosterRefresh()
        pendingRosterRefreshRunnable = Runnable {
            pendingRosterRefreshRunnable = null
            if (isCurrentSession(attemptId, sessionToken)) {
                refreshRoster()
            }
        }.also { mainHandler.postDelayed(it, delayMs.coerceAtLeast(100L)) }
    }

    private fun scheduleRetry(username: String, delayMs: Long, attemptId: Long, reason: String) {
        cancelPendingConnect("reschedule")
        pendingConnectRunnable = Runnable {
            pendingConnectRunnable = null
            if (ignoreStaleAttempt(attemptId, "reconnect-fire")) {
                return@Runnable
            }
            connectInternal(username, automatic = true)
        }.also {
            mainHandler.postDelayed(it, delayMs.coerceAtLeast(500L))
        }
        logTransport("reconnect-scheduled", attemptId, "in=${delayMs.coerceAtLeast(500L)}ms reason=$reason")
    }

    private fun scheduleReconnect(attemptId: Long, reason: String) {
        val username = uiState.selfNode?.username.orEmpty().ifBlank { prefs.lastUsername() }
        if (username.isBlank()) {
            return
        }
        scheduleRetry(username, RECONNECT_DELAY_MS, attemptId, reason)
    }

    private fun cancelPendingConnect(reason: String) {
        pendingConnectRunnable?.let {
            mainHandler.removeCallbacks(it)
            logTransport("reconnect-cancelled", activeAttemptId, reason)
        }
        pendingConnectRunnable = null
    }

    private fun cancelPendingRosterRefresh() {
        pendingRosterRefreshRunnable?.let { mainHandler.removeCallbacks(it) }
        pendingRosterRefreshRunnable = null
    }

    private fun handleApiError(error: MeshRestClient.ApiError, reconnectMessage: String): Boolean {
        if (error.code != "INVALID_SESSION") {
            return false
        }
        recoverInvalidSession(reconnectMessage)
        return true
    }

    private fun recoverInvalidSession(reason: String) {
        val username = uiState.selfNode?.username.ifNullOrBlank { prefs.lastUsername() }
        prefs.clearSession()
        cancelPendingConnect("invalid-session")
        cancelPendingRosterRefresh()
        activeAttemptId = 0L
        wsClient.disconnect("invalid-session")
        uiState = uiState.copy(
            connectionState = ConnectionState.Reconnecting,
            connectionMessage = reason,
            transportAttemptId = 0,
            selfNode = null,
            roster = emptyList(),
            selectedPeer = null,
            draftMessage = "",
            sendInProgress = false,
            lastError = reason
        )
        if (username.isNotBlank()) {
            connectInternal(username, automatic = true)
        }
    }

    private fun resetSessionState() {
        cancelPendingConnect("reset")
        cancelPendingRosterRefresh()
        prefs.clearSession()
        activeAttemptId = 0L
        uiState = uiState.copy(
            connectionState = ConnectionState.Disconnected,
            connectionMessage = "",
            transportAttemptId = 0,
            selfNode = null,
            nodeStatus = NodeStatus(),
            roster = emptyList(),
            selectedPeer = null,
            draftMessage = "",
            sendInProgress = false,
            lastError = null
        )
    }

    private fun appendMessage(peerKey: String, message: ChatMessage) {
        conversations[peerKey] = conversations[peerKey].orEmpty() + message
    }

    private fun updateMessages(transform: (ChatMessage) -> ChatMessage) {
        conversations.keys.toList().forEach { key ->
            conversations[key] = conversations[key].orEmpty().map(transform)
        }
    }

    private fun addNotice(text: String) {
        val updated = (listOf(SystemNotice(System.currentTimeMillis(), text, System.currentTimeMillis())) + uiState.notices).take(12)
        uiState = uiState.copy(notices = updated)
    }

    private fun ignoreStaleAttempt(attemptId: Long, source: String): Boolean {
        if (attemptId == activeAttemptId) {
            return false
        }
        logTransport("connect-stale-ignored", attemptId, "$source active=$activeAttemptId")
        return true
    }

    private fun isCurrentSession(attemptId: Long, sessionToken: String): Boolean {
        return attemptId == activeAttemptId && uiState.selfNode?.sessionToken == sessionToken
    }

    private fun logTransport(event: String, attemptId: Long, details: String = "") {
        val suffix = if (details.isBlank()) "" else " $details"
        Log.d(TAG, "$event attempt=$attemptId$suffix")
    }

    private fun String?.ifNullOrBlank(fallback: () -> String): String {
        return if (this.isNullOrBlank()) fallback() else this
    }

    private companion object {
        const val TAG = "MeshTransport"
        const val RECONNECT_DELAY_MS = 3000L
    }
}
