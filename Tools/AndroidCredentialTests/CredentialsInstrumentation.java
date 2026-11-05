package com.lati.lightbox;

import android.app.Instrumentation;
import android.content.Context;
import android.os.Bundle;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.KeyStore;
import org.json.JSONObject;

/** Runs against Android's real Keystore; never uses the user's gateway credential. */
public final class CredentialsInstrumentation extends Instrumentation {
    private static final String NAMESPACE = "lightbox.test.credentials";
    private int checks;
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
    private void check(boolean condition, String description) {
        if (!condition) throw new AssertionError(description);
        ++checks;
    }
    @Override public void onStart() {
        Bundle result = new Bundle();
        int code = -1;
        Context context = getTargetContext();
        File file = new File(context.getNoBackupFilesDir(), NAMESPACE + ".json");
        try {
            file.delete();
            KeyStore existing=KeyStore.getInstance("AndroidKeyStore");existing.load(null);existing.deleteEntry(NAMESPACE);
            LightBoxCredentials vault = new LightBoxCredentials(context, NAMESPACE);
            String endpoint = "http://127.0.0.1:18879", other = "http://127.0.0.1:18880";
            String token = "fixture-credential-unique-never-real";
            check(vault.load(endpoint).isEmpty(), "Missing vault requires pairing");
            check(vault.save(endpoint, token), "Real Android Keystore encryption succeeds");
            check(new LightBoxCredentials(context, NAMESPACE).load(endpoint).equals(token), "New client restores credential");
            check(vault.load(other).isEmpty(), "Endpoint change does not reuse credential");
            String first = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
            check(!first.contains(token), "Private file contains no plaintext credential");
            check(vault.save(endpoint, token), "Credential can be updated");
            String second = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
            check(!first.equals(second), "Every encryption uses a fresh nonce");
            check(!vault.save(endpoint, "") && !vault.save(endpoint, "bad\nkey"), "Invalid input is rejected");
            check(vault.load(endpoint).equals(token), "Invalid save preserves valid credential");
            JSONObject changed = new JSONObject(second); changed.put("endpoint", other);
            Files.write(file.toPath(), changed.toString().getBytes(StandardCharsets.UTF_8));
            check(vault.load(other).isEmpty(), "Authenticated encryption rejects endpoint tampering");
            Files.write(file.toPath(), "broken".getBytes(StandardCharsets.UTF_8));
            check(vault.load(endpoint).isEmpty(), "Corrupt file fails closed");
            check(vault.save(endpoint, token), "Corrupt file can be repaired by re-pairing");
            KeyStore store = KeyStore.getInstance("AndroidKeyStore"); store.load(null); store.deleteEntry(NAMESPACE);
            check(vault.load(endpoint).isEmpty(), "Restored file without device key fails closed");
            result.putString("stream", "\nCredential checks passed: " + checks + "\n");
        } catch (Throwable error) {
            result.putString("stream", "\nCredential test failed: " + error.getClass().getSimpleName() + ": " + error.getMessage() + "\n");
            code = 1;
        } finally {
            file.delete();
            try { KeyStore store=KeyStore.getInstance("AndroidKeyStore");store.load(null);store.deleteEntry(NAMESPACE); } catch (Exception ignored) { }
        }
        finish(code, result);
    }
}
