package org.tpwport.android;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.net.ssl.HttpsURLConnection;

final class AndroidUpdates {
    static final String RELEASES = "https://github.com/ThemeParkWorldDecomp/tpw-port/releases";
    private static final String LATEST = "https://api.github.com/repos/ThemeParkWorldDecomp/tpw-port/releases/latest";
    private static final Pattern VERSION = Pattern.compile("(\\d{1,6})\\.(\\d{1,6})\\.(\\d{1,6})(-.+)?");
    private static final int MAX_BYTES = 256 * 1024;

    static String newest(String installed) {
        if (installed == null || !VERSION.matcher(installed).matches()) return null;
        HttpsURLConnection connection = null;
        try {
            connection = (HttpsURLConnection) new URL(LATEST).openConnection();
            connection.setConnectTimeout(2500);
            connection.setReadTimeout(2500);
            connection.setInstanceFollowRedirects(false);
            connection.setRequestProperty("Accept", "application/vnd.github+json");
            if (connection.getResponseCode() != 200) return null;
            long deadline = System.nanoTime() + 5_000_000_000L;
            try (InputStream input = connection.getInputStream(); ByteArrayOutputStream data = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[8192];
                for (int n; (n = input.read(buffer)) != -1;) {
                    if (data.size() + n > MAX_BYTES || System.nanoTime() > deadline) return null;
                    data.write(buffer, 0, n);
                }
                return findUpdate(new JSONObject(new String(data.toByteArray(), StandardCharsets.UTF_8)), installed);
            }
        } catch (IOException | JSONException ignored) {
            return null;
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    private static String findUpdate(JSONObject release, String installed) throws JSONException {
        String tag = release.getString("tag_name");
        if (!tag.startsWith("v") || release.optBoolean("draft") || release.optBoolean("prerelease")) return null;
        Matcher candidate = VERSION.matcher(tag.substring(1));
        Matcher current = VERSION.matcher(installed);
        if (!candidate.matches() || candidate.group(4) != null || !current.matches()) return null;
        int newer = 0;
        for (int part = 1; part <= 3 && newer == 0; ++part) {
            newer = Integer.compare(Integer.parseInt(candidate.group(part)), Integer.parseInt(current.group(part)));
        }
        if (newer < 0 || (newer == 0 && current.group(4) == null)) return null;
        JSONArray assets = release.getJSONArray("assets");
        for (int i = 0; i < assets.length(); ++i) {
            JSONObject asset = assets.getJSONObject(i);
            String file = asset.optString("name");
            if (file.endsWith(".apk") && asset.optString("state").equals("uploaded")) return tag.substring(1);
        }
        return null;
    }

    private AndroidUpdates() { }
}
