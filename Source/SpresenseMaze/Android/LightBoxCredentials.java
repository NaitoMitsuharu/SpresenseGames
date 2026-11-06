package com.lati.lightbox;

import android.content.Context;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;
import android.util.Base64;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import org.json.JSONObject;

/** Gateway credentials stay encrypted in app-private, non-backup storage. */
public final class LightBoxCredentials {
    private final AtomicFile file;
    private final String alias;

    public LightBoxCredentials(Context context) { this(context, "lightbox.gateway.v1"); }

    // Separate namespace lets on-device tests exercise real Keystore operations in isolation.
    LightBoxCredentials(Context context, String namespace) {
        alias = namespace;
        file = new AtomicFile(new File(context.getNoBackupFilesDir(), namespace + ".json"));
    }

    private SecretKey key(boolean create) throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (store.containsAlias(alias)) return (SecretKey) store.getKey(alias, null);
        if (!create) return null;
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(alias,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256).build());
        return generator.generateKey();
    }

    private static boolean valid(String token) {
        return token != null && !token.trim().isEmpty() && token.length() <= 2048
                && !token.contains("\r") && !token.contains("\n");
    }

    public synchronized boolean save(String endpoint, String token) {
        if (endpoint == null || endpoint.isEmpty() || !valid(token)) return false;
        FileOutputStream stream = null;
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key(true));
            cipher.updateAAD(endpoint.getBytes(StandardCharsets.UTF_8));
            JSONObject record = new JSONObject();
            record.put("version", 1);
            record.put("endpoint", endpoint);
            record.put("iv", Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP));
            record.put("ciphertext", Base64.encodeToString(
                    cipher.doFinal(token.trim().getBytes(StandardCharsets.UTF_8)), Base64.NO_WRAP));
            stream = file.startWrite();
            stream.write(record.toString().getBytes(StandardCharsets.UTF_8));
            file.finishWrite(stream);
            return true;
        } catch (Exception ignored) {
            if (stream != null) file.failWrite(stream);
            return false;
        }
    }

    /** Extras sent by Tools/OpenClaw/start-lightbox.sh through an explicit `am start`. */
    public static final String EXTRA_ENDPOINT = "com.lati.lightbox.GATEWAY_ENDPOINT";
    public static final String EXTRA_TOKEN = "com.lati.lightbox.GATEWAY_TOKEN";

    // Same rule as ULightBoxDirector::ValidateEndpoint: on-device Gateway only.
    private static boolean loopbackEndpoint(String endpoint) {
        return endpoint != null
                && endpoint.matches("http://(127\\.0\\.0\\.1|localhost):[0-9]{1,5}")
                && Integer.parseInt(endpoint.substring(endpoint.lastIndexOf(':') + 1)) > 0
                && Integer.parseInt(endpoint.substring(endpoint.lastIndexOf(':') + 1)) <= 65535;
    }

    /**
     * Stores the Gateway key handed over by the Termux launcher, but only when no
     * key is saved for that endpoint yet: a key the user saved is never replaced
     * by an intent. The extras are always removed so the key does not linger in
     * the activity's intent.
     *
     * @return true when a new key was stored.
     */
    public static boolean acceptProvisioning(Context context, android.content.Intent intent) {
        if (intent == null || !intent.hasExtra(EXTRA_TOKEN)) return false;
        String token;
        String endpoint;
        try {
            token = intent.getStringExtra(EXTRA_TOKEN);
            endpoint = intent.getStringExtra(EXTRA_ENDPOINT);
        } catch (RuntimeException malformed) {
            token = null;
            endpoint = null;
        } finally {
            intent.removeExtra(EXTRA_TOKEN);
            intent.removeExtra(EXTRA_ENDPOINT);
        }
        if (endpoint == null) endpoint = "http://127.0.0.1:18789";
        if (!loopbackEndpoint(endpoint) || !valid(token)) return false;
        LightBoxCredentials vault = new LightBoxCredentials(context);
        // One record per vault: a working key for any endpoint counts as "set".
        String saved = vault.savedEndpoint();
        if (!saved.isEmpty() && !vault.load(saved).isEmpty()) return false;
        return vault.save(endpoint, token);
    }

    private synchronized String savedEndpoint() {
        try {
            return new JSONObject(new String(file.readFully(), StandardCharsets.UTF_8))
                    .optString("endpoint", "");
        } catch (Exception missing) {
            return "";
        }
    }

    public synchronized String load(String endpoint) {
        try {
            JSONObject record = new JSONObject(new String(file.readFully(), StandardCharsets.UTF_8));
            if (record.getInt("version") != 1 || !endpoint.equals(record.getString("endpoint"))) return "";
            SecretKey secret = key(false);
            if (secret == null) return "";
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, secret, new GCMParameterSpec(128,
                    Base64.decode(record.getString("iv"), Base64.NO_WRAP)));
            cipher.updateAAD(endpoint.getBytes(StandardCharsets.UTF_8));
            String token = new String(cipher.doFinal(Base64.decode(
                    record.getString("ciphertext"), Base64.NO_WRAP)), StandardCharsets.UTF_8);
            return valid(token) ? token : "";
        } catch (Exception ignored) {
            // A missing/restored/corrupt vault needs re-pairing; never log credential data.
            return "";
        }
    }
}
