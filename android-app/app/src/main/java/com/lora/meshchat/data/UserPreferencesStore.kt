package com.lora.meshchat.data

import android.content.Context
import java.security.MessageDigest
import java.util.UUID

class UserPreferencesStore(context: Context) {
    private val prefs = context.getSharedPreferences("mesh_prefs", Context.MODE_PRIVATE)

    fun lastUsername(): String = prefs.getString(KEY_USERNAME, "") ?: ""

    fun saveUsername(username: String) {
        prefs.edit().putString(KEY_USERNAME, username).apply()
    }

    fun stableClientId(): String {
        val existing = prefs.getString(KEY_STABLE_CLIENT_ID, null)
        if (!existing.isNullOrBlank()) {
            return existing
        }
        val generated = UUID.randomUUID().toString()
        prefs.edit().putString(KEY_STABLE_CLIENT_ID, generated).apply()
        return generated
    }

    fun sessionToken(): String? {
        return prefs.getString(KEY_SESSION_TOKEN, null)?.takeIf { it.isNotBlank() }
    }

    fun nodeId(): String? {
        return prefs.getString(KEY_NODE_ID, null)?.takeIf { it.isNotBlank() }
    }

    fun displayName(): String? {
        return prefs.getString(KEY_DISPLAY_NAME, null)?.takeIf { it.isNotBlank() }
    }

    fun pseudoMac(): String {
        val bytes = MessageDigest.getInstance("SHA-256").digest(stableClientId().toByteArray())
        return bytes.take(6).joinToString(":") { byte -> "%02X".format(byte.toInt() and 0xFF) }
    }

    fun saveSession(sessionToken: String, nodeId: String, displayName: String) {
        prefs.edit()
            .putString(KEY_SESSION_TOKEN, sessionToken)
            .putString(KEY_NODE_ID, nodeId)
            .putString(KEY_DISPLAY_NAME, displayName)
            .apply()
    }

    fun clearSession() {
        prefs.edit()
            .remove(KEY_SESSION_TOKEN)
            .remove(KEY_NODE_ID)
            .remove(KEY_DISPLAY_NAME)
            .apply()
    }

    private companion object {
        const val KEY_USERNAME = "last_username"
        const val KEY_STABLE_CLIENT_ID = "stable_client_id"
        const val KEY_SESSION_TOKEN = "session_token"
        const val KEY_NODE_ID = "node_id"
        const val KEY_DISPLAY_NAME = "display_name"
    }
}
