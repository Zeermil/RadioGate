package com.lora.meshchat.data

enum class ConnectionState {
    Disconnected,
    Connecting,
    CheckingNickname,
    Connected,
    Reconnecting,
    Offline,
    Leaving
}

enum class DeliveryState {
    Draft,
    Sending,
    Accepted,
    Delivered,
    Failed
}

data class SelfNodeInfo(
    val username: String,
    val stableClientId: String,
    val nodeId: String,
    val displayName: String,
    val sessionToken: String
)

data class NodeStatus(
    val nodeId: String = "",
    val nodeHash: String = "",
    val efuseMacSuffix: String = "",
    val displayName: String = "",
    val uptimeMs: Long = 0,
    val localClients: Int = 0,
    val knownClients: Int = 0,
    val remoteClients: Int = 0,
    val neighbors: Int = 0,
    val routes: Int = 0,
    val wsClients: Int = 0,
    val frequencyHz: Long = 0,
    val bandwidthHz: Long = 0,
    val spreadingFactor: Int = 0,
    val codingRate: Int = 0,
    val txPower: Int = 0,
    val syncWord: Int = 0,
    val meshAddress: Int = 0,
    val freeHeap: Long = 0,
    val minFreeHeap: Long = 0,
    val freePsram: Long = 0,
    val loraQueue: Int = 0,
    val meshQueueDepth: Int = 0,
    val presenceIntervalMs: Long = 0,
    val seenPackets: Int = 0,
    val wsAwaitingPong: Int = 0,
    val lastWsPingMs: Long = 0,
    val lastWsPongMs: Long = 0,
    val loraTxCount: Long = 0,
    val loraRxCount: Long = 0,
    val lastLoRaRssi: Int = 0,
    val lastLoRaSnr: Double = 0.0,
    val lastRadioActivityMs: Long = 0,
    val lastRadioError: String = "none",
    val lastMeshPacketType: String = "",
    val lastMeshFrom: String = "",
    val lastMeshTo: String = "",
    val lastPresenceUsers: Int = 0,
    val lastPresenceFrom: String = "",
    val lastSyncClientCount: Int = 0,
    val lastSyncFrom: String = "",
    val selfOriginEchoDrops: Long = 0,
    val firmwareBuildId: String = "",
    val transportRevision: String = "",
    val lastError: String = "none"
)

data class RosterEntry(
    val username: String,
    val homeNode: String,
    val displayNode: String,
    val isOnline: Boolean,
    val isLocal: Boolean
) {
    val key: String = username
}

sealed interface NodeEvent {
    val id: Long

    data class Message(
        override val id: Long,
        val messageId: String,
        val from: String,
        val text: String,
        val timestampMs: Long
    ) : NodeEvent

    data class MessageStatus(
        override val id: Long,
        val messageId: String,
        val status: String,
        val reason: String?
    ) : NodeEvent

    data class Network(
        override val id: Long,
        val event: String,
        val username: String,
        val homeNode: String?,
        val displayNode: String?,
        val online: Boolean?
    ) : NodeEvent

    data class System(
        override val id: Long,
        val text: String
    ) : NodeEvent
}

data class ConnectSession(
    val username: String,
    val nodeId: String,
    val displayName: String,
    val sessionToken: String,
    val sessionTtlSec: Long
)

data class SendAccepted(
    val messageId: String,
    val deliveryStatus: String
)

data class ChatMessage(
    val localId: String,
    val peerKey: String,
    val fromSelf: Boolean,
    val text: String,
    val timestampMs: Long,
    val deliveryState: DeliveryState = DeliveryState.Draft,
    val remoteMessageId: String? = null
)

data class SystemNotice(
    val id: Long,
    val text: String,
    val timestampMs: Long
)
