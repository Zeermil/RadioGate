package com.lora.meshchat.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.lora.meshchat.BuildConfig
import com.lora.meshchat.data.ChatMessage
import com.lora.meshchat.data.ConnectionState
import com.lora.meshchat.data.DeliveryState
import com.lora.meshchat.data.NodeStatus
import com.lora.meshchat.data.RosterEntry
import com.lora.meshchat.data.SelfNodeInfo
import com.lora.meshchat.data.SystemNotice
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun MeshChatApp(viewModel: AppViewModel) {
    val state = viewModel.uiState

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        Color(0xFFF2E8D7),
                        Color(0xFFE2F0EE),
                        Color(0xFFF7F7F2)
                    )
                )
            )
    ) {
        when {
            state.selectedPeer != null && state.selfNode != null -> ChatScreen(
                state = state,
                messages = viewModel.selectedMessages,
                onBack = viewModel::backToRoster,
                onDraftChanged = viewModel::onDraftChanged,
                onSend = viewModel::sendDraft,
                onReconnect = viewModel::reconnectTransport,
                onLeave = viewModel::leave
            )

            state.selfNode != null || state.connectionState != ConnectionState.Disconnected -> MainScreen(
                state = state,
                onReconnect = viewModel::reconnectTransport,
                onRefreshUsers = viewModel::refreshUsers,
                onRefreshNode = viewModel::refreshNode,
                onLeave = viewModel::leave,
                onSelectPeer = viewModel::openChat
            )

            else -> LoginScreen(
                state = state,
                onUsernameChanged = viewModel::onUsernameChanged,
                onConnect = viewModel::connect,
                onRetry = viewModel::retryConnection
            )
        }
    }
}

@Composable
private fun LoginScreen(
    state: AppUiState,
    onUsernameChanged: (String) -> Unit,
    onConnect: () -> Unit,
    onRetry: () -> Unit
) {
    val scrollState = rememberScrollState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .statusBarsPadding()
            .navigationBarsPadding()
            .imePadding()
            .verticalScroll(scrollState)
            .padding(24.dp)
    ) {
        Text(
            text = "RadioGate Mesh",
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.Bold,
            color = Color(0xFF17343D)
        )
        Spacer(modifier = Modifier.height(10.dp))
        Text(
            text = "Join the node Wi-Fi, choose a nickname, then keep chats and user refresh separate from reconnect.",
            color = Color(0xFF416069),
            style = MaterialTheme.typography.bodyLarge
        )
        Spacer(modifier = Modifier.height(20.dp))

        StatusCard(
            connectionState = state.connectionState,
            connectionMessage = state.connectionMessage,
            attemptId = state.transportAttemptId,
            selfNode = null,
            nodeStatus = state.nodeStatus,
            errorText = state.lastError
        )

        if (state.connectionState == ConnectionState.Offline) {
            Spacer(modifier = Modifier.height(8.dp))
            TextButton(onClick = onRetry) {
                Text("Retry last session")
            }
        }

        Spacer(modifier = Modifier.height(18.dp))
        OutlinedTextField(
            value = state.usernameInput,
            onValueChange = onUsernameChanged,
            modifier = Modifier.fillMaxWidth(),
            label = { Text("Nickname") },
            supportingText = { Text("3-23 chars: letters, numbers, -, _") },
            singleLine = true,
            shape = RoundedCornerShape(18.dp)
        )
        Spacer(modifier = Modifier.height(14.dp))
        Button(
            onClick = onConnect,
            modifier = Modifier.fillMaxWidth(),
            enabled = state.connectionState !in setOf(ConnectionState.Connecting, ConnectionState.CheckingNickname, ConnectionState.Leaving)
        ) {
            Text(
                when (state.connectionState) {
                    ConnectionState.CheckingNickname -> "Checking..."
                    ConnectionState.Connecting -> "Connecting..."
                    else -> "Connect"
                }
            )
        }
    }
}

@Composable
private fun MainScreen(
    state: AppUiState,
    onReconnect: () -> Unit,
    onRefreshUsers: () -> Unit,
    onRefreshNode: () -> Unit,
    onLeave: () -> Unit,
    onSelectPeer: (RosterEntry) -> Unit
) {
    val local = state.roster.filter { it.isLocal }
    val mesh = state.roster.filterNot { it.isLocal }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .statusBarsPadding()
            .navigationBarsPadding()
            .imePadding(),
        contentPadding = PaddingValues(horizontal = 18.dp, vertical = 14.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item("status") {
            StatusCard(
                connectionState = state.connectionState,
                connectionMessage = state.connectionMessage,
                attemptId = state.transportAttemptId,
                selfNode = state.selfNode,
                nodeStatus = state.nodeStatus,
                errorText = state.lastError
            )
        }

        item("actions") {
            MainActionPanel(
                canReconnect = state.connectionState !in setOf(ConnectionState.Connecting, ConnectionState.CheckingNickname, ConnectionState.Leaving),
                canRefreshUsers = state.selfNode != null,
                canRefreshNode = true,
                onReconnect = onReconnect,
                onRefreshUsers = onRefreshUsers,
                onRefreshNode = onRefreshNode,
                onLeave = onLeave
            )
        }

        item("notices") {
            NoticeCard(state.notices)
        }

        if (state.roster.isEmpty()) {
            item("empty-roster") {
                Card(
                    shape = RoundedCornerShape(24.dp),
                    colors = CardDefaults.cardColors(containerColor = Color(0xCCFFFFFF))
                ) {
                    Text(
                        text = "No peers visible yet. Use Refresh Users if someone just connected, or wait for the next network event.",
                        modifier = Modifier.padding(20.dp),
                        color = Color(0xFF4C676F)
                    )
                }
            }
        } else {
            if (local.isNotEmpty()) {
                item("local-title") { SectionTitle("Local peers") }
                items(local, key = { it.key }) { peer -> UserRow(peer, onClick = { onSelectPeer(peer) }) }
            }
            if (mesh.isNotEmpty()) {
                item("mesh-title") { SectionTitle("Mesh peers") }
                items(mesh, key = { it.key }) { peer -> UserRow(peer, onClick = { onSelectPeer(peer) }) }
            }
        }
    }
}

@Composable
private fun ChatScreen(
    state: AppUiState,
    messages: List<ChatMessage>,
    onBack: () -> Unit,
    onDraftChanged: (String) -> Unit,
    onSend: () -> Unit,
    onReconnect: () -> Unit,
    onLeave: () -> Unit
) {
    val peer = state.selectedPeer ?: return
    val listState = rememberLazyListState()
    val chatStaticItems = 3

    LaunchedEffect(messages.size) {
        val targetIndex = if (messages.isEmpty()) chatStaticItems else chatStaticItems + messages.lastIndex
        listState.animateScrollToItem(targetIndex)
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .statusBarsPadding()
            .navigationBarsPadding()
            .imePadding()
            .padding(horizontal = 18.dp, vertical = 14.dp)
    ) {
        LazyColumn(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth(),
            state = listState,
            verticalArrangement = Arrangement.spacedBy(12.dp),
            contentPadding = PaddingValues(bottom = 12.dp)
        ) {
            item("chat-header") {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    TextButton(onClick = onBack) { Text("Back") }
                    Column(horizontalAlignment = Alignment.End) {
                        Text(peer.username, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, color = Color(0xFF17343D))
                        Text("${peer.displayNode} В· ${peer.homeNode}", color = Color(0xFF4F6A71))
                    }
                }
            }

            item("chat-status") {
                StatusCard(
                    connectionState = state.connectionState,
                    connectionMessage = state.connectionMessage,
                    attemptId = state.transportAttemptId,
                    selfNode = state.selfNode,
                    nodeStatus = state.nodeStatus,
                    errorText = state.lastError,
                    compact = true
                )
            }

            item("chat-actions") {
                ChatActionPanel(
                    canReconnect = state.connectionState !in setOf(ConnectionState.Connecting, ConnectionState.CheckingNickname, ConnectionState.Leaving),
                    onReconnect = onReconnect,
                    onLeave = onLeave
                )
            }

            if (messages.isEmpty()) {
                item("empty-messages") {
                    Card(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(220.dp),
                        shape = RoundedCornerShape(24.dp),
                        colors = CardDefaults.cardColors(containerColor = Color(0xCCFFFFFF))
                    ) {
                        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                            Text(
                                text = "No messages yet.",
                                color = Color(0xFF567178)
                            )
                        }
                    }
                }
            } else {
                items(messages, key = { it.localId }) { message ->
                    MessageBubble(message)
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = state.draftMessage,
                onValueChange = onDraftChanged,
                modifier = Modifier.weight(1f),
                label = { Text("Message") },
                maxLines = 4,
                shape = RoundedCornerShape(18.dp)
            )
            Spacer(modifier = Modifier.width(10.dp))
            Button(
                onClick = onSend,
                enabled = state.connectionState == ConnectionState.Connected && !state.sendInProgress && state.draftMessage.isNotBlank()
            ) {
                Text(if (state.sendInProgress) "..." else "Send")
            }
        }
    }
}

@Composable
private fun MainActionPanel(
    canReconnect: Boolean,
    canRefreshUsers: Boolean,
    canRefreshNode: Boolean,
    onReconnect: () -> Unit,
    onRefreshUsers: () -> Unit,
    onRefreshNode: () -> Unit,
    onLeave: () -> Unit
) {
    Card(shape = RoundedCornerShape(24.dp), colors = CardDefaults.cardColors(containerColor = Color(0xB8FFFFFF))) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                FilledTonalButton(
                    onClick = onReconnect,
                    modifier = Modifier.weight(1f),
                    enabled = canReconnect
                ) {
                    Text("Reconnect")
                }
                OutlinedButton(
                    onClick = onRefreshUsers,
                    modifier = Modifier.weight(1f),
                    enabled = canRefreshUsers
                ) {
                    Text("Refresh users")
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                OutlinedButton(
                    onClick = onRefreshNode,
                    modifier = Modifier.weight(1f),
                    enabled = canRefreshNode
                ) {
                    Text("Refresh node")
                }
                Button(
                    onClick = onLeave,
                    modifier = Modifier.weight(1f)
                ) {
                    Text("Leave")
                }
            }
        }
    }
}

@Composable
private fun ChatActionPanel(
    canReconnect: Boolean,
    onReconnect: () -> Unit,
    onLeave: () -> Unit
) {
    Card(shape = RoundedCornerShape(24.dp), colors = CardDefaults.cardColors(containerColor = Color(0xB8FFFFFF))) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            FilledTonalButton(
                onClick = onReconnect,
                modifier = Modifier.weight(1f),
                enabled = canReconnect
            ) {
                Text("Reconnect")
            }
            Button(
                onClick = onLeave,
                modifier = Modifier.weight(1f)
            ) {
                Text("Leave")
            }
        }
    }
}

@Composable
private fun StatusCard(
    connectionState: ConnectionState,
    connectionMessage: String,
    attemptId: Long,
    selfNode: SelfNodeInfo?,
    nodeStatus: NodeStatus,
    errorText: String?,
    compact: Boolean = false
) {
    Card(shape = RoundedCornerShape(24.dp), colors = CardDefaults.cardColors(containerColor = Color(0xCCFFFFFF))) {
        Column(modifier = Modifier.padding(18.dp)) {
            Text(
                text = selfNode?.username ?: "Disconnected",
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = Color(0xFF17343D)
            )
            Text(statusTitle(connectionState), color = statusColor(connectionState))
            if (connectionMessage.isNotBlank()) {
                Text(connectionMessage, color = Color(0xFF607B80), style = MaterialTheme.typography.bodySmall)
            }

            Spacer(modifier = Modifier.height(10.dp))
            Text(
                text = buildString {
                    append(selfNode?.displayName ?: "Node ?")
                    if (selfNode != null) {
                        append(" | ")
                        append(selfNode.nodeId)
                    }
                },
                color = Color(0xFF48636B)
            )

            Spacer(modifier = Modifier.height(10.dp))
            if (compact) {
                Text(
                    text = "clients ${nodeStatus.localClients}/${nodeStatus.knownClients} | remote ${nodeStatus.remoteClients} | ws ${nodeStatus.wsClients}",
                    color = Color(0xFF48636B),
                    style = MaterialTheme.typography.bodySmall
                )
                Text(
                    text = "${formatModemProfile(nodeStatus)} | mesh ${formatMeshAddress(nodeStatus.meshAddress)}",
                    color = Color(0xFF48636B),
                    style = MaterialTheme.typography.bodySmall
                )
                Text(
                    text = "neighbors ${nodeStatus.neighbors} | presence ${nodeStatus.lastPresenceUsers} | every ${formatInterval(nodeStatus.presenceIntervalMs)}",
                    color = Color(0xFF48636B),
                    style = MaterialTheme.typography.bodySmall
                )
                Text(
                    text = "hash ${nodeStatus.nodeHash.ifBlank { "-" }} | from ${nodeStatus.lastPresenceFrom.ifBlank { "-" }} | echo ${nodeStatus.selfOriginEchoDrops}",
                    color = Color(0xFF48636B),
                    style = MaterialTheme.typography.bodySmall
                )
            } else {
                Text("rf ${formatFrequency(nodeStatus.frequencyHz)} | mesh ${formatMeshAddress(nodeStatus.meshAddress)} | routes ${nodeStatus.routes} | ws ${nodeStatus.wsClients}", color = Color(0xFF48636B))
                Text("${formatModemProfile(nodeStatus)} | sync 0x${nodeStatus.syncWord.toString(16).uppercase(Locale.US)}", color = Color(0xFF48636B))
                Text("hash ${nodeStatus.nodeHash.ifBlank { "-" }} | mac ${nodeStatus.efuseMacSuffix.ifBlank { "-" }} | remote ${nodeStatus.remoteClients}", color = Color(0xFF48636B))
                Text("presence ${nodeStatus.lastPresenceUsers} | from ${nodeStatus.lastPresenceFrom.ifBlank { "-" }} | every ${formatInterval(nodeStatus.presenceIntervalMs)} | echo ${nodeStatus.selfOriginEchoDrops}", color = Color(0xFF48636B))
                Text("tx ${nodeStatus.loraTxCount} | rx ${nodeStatus.loraRxCount} | rssi ${nodeStatus.lastLoRaRssi} | snr ${String.format(Locale.US, "%.1f", nodeStatus.lastLoRaSnr)}", color = Color(0xFF48636B))
                Text("heap ${nodeStatus.freeHeap} | min ${nodeStatus.minFreeHeap} | psram ${nodeStatus.freePsram}", color = Color(0xFF48636B))
                Text("clients ${nodeStatus.localClients}/${nodeStatus.knownClients} | neighbors ${nodeStatus.neighbors} | awaitPong ${nodeStatus.wsAwaitingPong}", color = Color(0xFF48636B))
                Text("queue ${nodeStatus.meshQueueDepth} | seen ${nodeStatus.seenPackets} | uptime ${formatAgo(nodeStatus.uptimeMs)}", color = Color(0xFF48636B))
                Text("mesh ${nodeStatus.lastMeshPacketType.ifBlank { "-" }} | from ${nodeStatus.lastMeshFrom.ifBlank { "-" }} | to ${nodeStatus.lastMeshTo.ifBlank { "-" }}", color = Color(0xFF48636B))
                Text("radio ${nodeStatus.lastRadioError.ifBlank { "none" }} | last ${formatWsAge(nodeStatus.uptimeMs, nodeStatus.lastRadioActivityMs)}", color = Color(0xFF48636B))
            }

            Spacer(modifier = Modifier.height(10.dp))
            Text(
                text = "app ${BuildConfig.APP_BUILD_ID} | fw ${nodeStatus.firmwareBuildId.ifBlank { "?" }}",
                color = Color(0xFF48636B),
                style = MaterialTheme.typography.bodySmall
            )
            Text(
                text = "tr ${nodeStatus.transportRevision.ifBlank { BuildConfig.TRANSPORT_REVISION }} | attempt $attemptId | ping ${formatWsAge(nodeStatus.uptimeMs, nodeStatus.lastWsPingMs)} | pong ${formatWsAge(nodeStatus.uptimeMs, nodeStatus.lastWsPongMs)}",
                color = Color(0xFF48636B),
                style = MaterialTheme.typography.bodySmall
            )

            if (!errorText.isNullOrBlank()) {
                Spacer(modifier = Modifier.height(10.dp))
                Text(errorText, color = Color(0xFF9B2C2C), style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun NoticeCard(notices: List<SystemNotice>) {
    var expanded by remember(notices.size) { mutableStateOf(false) }

    Card(shape = RoundedCornerShape(24.dp), colors = CardDefaults.cardColors(containerColor = Color(0xB8FFFFFF))) {
        Column(modifier = Modifier.padding(18.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Network events", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold, color = Color(0xFF21444C))
                if (notices.isNotEmpty()) {
                    TextButton(onClick = { expanded = !expanded }) {
                        Text(if (expanded) "Hide" else "Show")
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))
            if (notices.isEmpty()) {
                Text("No events yet", color = Color(0xFF5A737A))
            } else {
                val visibleNotices = if (expanded) notices.take(8) else notices.take(1)
                visibleNotices.forEachIndexed { index, notice ->
                    if (index > 0) {
                        HorizontalDivider(color = Color(0x14000000))
                    }
                    Text(
                        text = "${formatClock(notice.timestampMs)} В· ${notice.text}",
                        modifier = Modifier.padding(vertical = 6.dp),
                        color = Color(0xFF456067),
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        }
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(text = text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold, color = Color(0xFF21444C))
}

@Composable
private fun UserRow(entry: RosterEntry, onClick: () -> Unit) {
    Card(
        onClick = onClick,
        shape = RoundedCornerShape(22.dp),
        colors = CardDefaults.cardColors(containerColor = Color(0xCCFFFFFF))
    ) {
        Column(modifier = Modifier.padding(18.dp)) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Column {
                    Text(entry.username, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold, color = Color(0xFF16343C))
                    Text("${entry.displayNode} В· ${entry.homeNode}", color = Color(0xFF5A757C))
                }
                Text(if (entry.isLocal) "LOCAL" else "MESH", color = if (entry.isLocal) Color(0xFF0D6E67) else Color(0xFF566F75))
            }
            Spacer(modifier = Modifier.height(6.dp))
            Text(if (entry.isOnline) "online" else "offline", color = Color(0xFF5A757C), style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun MessageBubble(message: ChatMessage) {
    val background = if (message.fromSelf) Color(0xFFDDEEE9) else Color(0xFFF3ECE1)
    val alignment = if (message.fromSelf) Alignment.End else Alignment.Start
    Column(modifier = Modifier.fillMaxWidth(), horizontalAlignment = alignment) {
        Surface(shape = RoundedCornerShape(20.dp), color = background) {
            Column(modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp)) {
                Text(message.text, color = Color(0xFF17343D))
                Spacer(modifier = Modifier.height(6.dp))
                Text(
                    text = "${formatClock(message.timestampMs)} В· ${deliveryLabel(message.deliveryState)}",
                    color = Color(0xFF5B747B),
                    style = MaterialTheme.typography.bodySmall
                )
            }
        }
    }
}

private fun statusTitle(state: ConnectionState): String = when (state) {
    ConnectionState.Disconnected -> "Disconnected"
    ConnectionState.Connecting -> "Connecting"
    ConnectionState.CheckingNickname -> "Checking nickname"
    ConnectionState.Connected -> "Connected"
    ConnectionState.Reconnecting -> "Reconnecting"
    ConnectionState.Offline -> "Offline"
    ConnectionState.Leaving -> "Leaving"
}

private fun statusColor(state: ConnectionState): Color = when (state) {
    ConnectionState.Connected -> Color(0xFF0D6E67)
    ConnectionState.CheckingNickname -> Color(0xFF946200)
    ConnectionState.Connecting, ConnectionState.Reconnecting -> Color(0xFF2F5D9A)
    ConnectionState.Leaving -> Color(0xFF8C5E00)
    ConnectionState.Offline, ConnectionState.Disconnected -> Color(0xFF9B2C2C)
}

private fun deliveryLabel(state: DeliveryState): String = when (state) {
    DeliveryState.Draft -> "draft"
    DeliveryState.Sending -> "sending"
    DeliveryState.Accepted -> "accepted"
    DeliveryState.Delivered -> "delivered"
    DeliveryState.Failed -> "failed"
}

private fun formatClock(timestampMs: Long): String {
    val formatter = SimpleDateFormat("HH:mm", Locale.getDefault())
    return formatter.format(Date(timestampMs))
}

private fun formatAgo(valueMs: Long): String {
    val seconds = valueMs / 1000L
    val minutes = seconds / 60L
    val hours = minutes / 60L
    return when {
        hours > 0 -> "${hours}h"
        minutes > 0 -> "${minutes}m"
        else -> "${seconds}s"
    }
}

private fun formatFrequency(frequencyHz: Long): String {
    if (frequencyHz <= 0L) {
        return "?"
    }
    return String.format(Locale.US, "%.3f MHz", frequencyHz / 1_000_000.0)
}

private fun formatMeshAddress(meshAddress: Int): String {
    if (meshAddress <= 0) {
        return "-"
    }
    return String.format(Locale.US, "%04X", meshAddress)
}

private fun formatModemProfile(status: NodeStatus): String {
    val bandwidthKhz = if (status.bandwidthHz > 0L) status.bandwidthHz / 1000L else 0L
    val spreadingFactor = if (status.spreadingFactor > 0) "SF${status.spreadingFactor}" else "SF?"
    val codingRate = if (status.codingRate > 0) "CR${status.codingRate}" else "CR?"
    val power = if (status.txPower > 0) "P${status.txPower}" else "P?"
    val bandwidth = if (bandwidthKhz > 0L) "BW${bandwidthKhz}" else "BW?"
    return "$bandwidth / $spreadingFactor / $codingRate / $power"
}

private fun formatInterval(valueMs: Long): String {
    if (valueMs <= 0L) {
        return "-"
    }
    val seconds = valueMs / 1000L
    return if (seconds < 60L) {
        "${seconds}s"
    } else {
        "${seconds / 60L}m"
    }
}

private fun formatWsAge(uptimeMs: Long, stampMs: Long): String {
    if (stampMs <= 0L || uptimeMs <= 0L || stampMs > uptimeMs) {
        return "-"
    }
    return formatAgo(uptimeMs - stampMs)
}
