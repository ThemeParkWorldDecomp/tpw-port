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
    private static final String API = "https://api.github.com/repos/ThemeParkWorldDecomp/tpw-port/releases";
    private static final Pattern VERSION = Pattern.compile("(\\d{1,6})\\.(\\d{1,6})\\.(\\d{1,6})(-.+)?");
    private static final Pattern APK_NAME = Pattern.compile("[A-Za-z0-9][A-Za-z0-9._-]*\\.apk");
    private static final int PAGE_SIZE = 10, PAGE_LIMIT = 8, MAX_BYTES = 512 * 1024;

    static final class Release {
        final String version, download;
        Release(String version, String download) {
            this.version = version;
            this.download = download;
        }
    }

    static Release newest(String installed, String[] abis) {
        if (installed == null || !VERSION.matcher(installed).matches()) return null;
        long deadline = System.nanoTime() + 8_000_000_000L;
        Release best = null;
        for (int page = 1; page <= PAGE_LIMIT && System.nanoTime() < deadline; ++page) {
            try {
                byte[] data = read(API + "?per_page=" + PAGE_SIZE + "&page=" + page,
                                   "application/vnd.github+json", MAX_BYTES, deadline);
                JSONArray releases = new JSONArray(new String(data, StandardCharsets.UTF_8));
                best = findUpdate(releases, installed, abis, best);
                if (releases.length() < PAGE_SIZE) break;
            } catch (IOException | JSONException ignored) {
                break;
            }
        }
        return best;
    }

    private static byte[] read(String url, String accept, int maxBytes, long deadline) throws IOException {
        HttpsURLConnection connection = null;
        try {
            connection = (HttpsURLConnection) new URL(url).openConnection();
            connection.setConnectTimeout(2500);
            connection.setReadTimeout(2500);
            connection.setInstanceFollowRedirects(false);
            connection.setRequestProperty("Accept", accept);
            if (connection.getResponseCode() != 200) throw new IOException("No response");
            try (InputStream input = connection.getInputStream(); ByteArrayOutputStream data = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[8192];
                for (int n; (n = input.read(buffer)) != -1;) {
                    if (data.size() + n > maxBytes || System.nanoTime() > deadline) throw new IOException("Response limit");
                    data.write(buffer, 0, n);
                }
                return data.toByteArray();
            }
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    static Release findUpdate(JSONArray releases, String installed, String[] abis, Release best) {
        for (int i = 0; i < releases.length(); ++i) {
            JSONObject release = releases.optJSONObject(i);
            if (release == null || release.optBoolean("draft", true) || release.optBoolean("prerelease", true)) continue;
            String tag = release.optString("tag_name");
            if (!tag.startsWith("v")) continue;
            String version = tag.substring(1);
            Matcher candidate = VERSION.matcher(version);
            if (!candidate.matches() || candidate.group(4) != null || !newer(candidate, best == null ? installed : best.version))
                continue;
            JSONArray assets = release.optJSONArray("assets");
            if (assets == null) continue;
            int rank = Integer.MAX_VALUE;
            String download = null;
            for (int j = 0; j < assets.length(); ++j) {
                JSONObject asset = assets.optJSONObject(j);
                if (asset == null || !asset.optString("state").equals("uploaded")) continue;
                String file = asset.optString("name");
                int supported = abiRank(file, abis);
                String url = RELEASES + "/download/" + tag + "/" + file;
                if (supported < rank && url.equals(asset.optString("browser_download_url"))) {
                    rank = supported;
                    download = url;
                }
            }
            if (download != null) best = new Release(version, download);
        }
        return best;
    }

    private static boolean newer(Matcher candidate, String installed) {
        Matcher current = VERSION.matcher(installed);
        if (!current.matches()) return false;
        for (int part = 1; part <= 3; ++part) {
            int order = Integer.compare(Integer.parseInt(candidate.group(part)), Integer.parseInt(current.group(part)));
            if (order != 0) return order > 0;
        }
        return current.group(4) != null;
    }

    private static int abiRank(String file, String[] abis) {
        if (!APK_NAME.matcher(file).matches()) return Integer.MAX_VALUE;
        String stem = file.substring(0, file.length() - 4).toLowerCase(java.util.Locale.ROOT);
        if (stem.matches(".*[._-](debug|unsigned|test)([._-].*)?")) return Integer.MAX_VALUE;
        for (int i = 0; i < abis.length; ++i) {
            if (stem.endsWith("-" + abis[i]) || (abis[i].equals("arm64-v8a") && stem.endsWith("-arm64"))) return i;
        }
        return stem.endsWith("-universal") ? abis.length : Integer.MAX_VALUE;
    }

    private AndroidUpdates() { }
}
