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
