package org.tpwport.android;

import android.content.Context;
import android.net.Uri;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.stream.Stream;

final class AndroidFiles {
    private AndroidFiles() {}

    private static native String nativeExportLog(String dataFolder, String tempFolder);
    private static native boolean nativeValidateAssetPack(String archive, String tempFolder);

    static String previewLog(Context context, Path stage) throws IOException {
        try (InputStream in = Files.newInputStream(exportLog(context, stage))) {
            byte[] bytes = new byte[16384];
            int count = 0;
            for (int read; count < bytes.length && (read = in.read(bytes, count, bytes.length - count)) > 0;) count += read;
            return new String(bytes, 0, count, StandardCharsets.UTF_8) + (in.read() == -1 ? "" : "\n…");
        }
    }

    static Path exportLog(Context context, Path stage) throws IOException {
        String name = nativeExportLog(context.getFilesDir().getPath(), stage.toString());
        if (name == null) throw new IOException("No log available to export");
        return stage.resolve(name);
    }

    static void copyToDocument(Context context, Path source, Uri destination) throws IOException {
        try (OutputStream out = context.getContentResolver().openOutputStream(destination, "wt")) {
            if (out == null) throw new IOException("Document stream unavailable");
            Files.copy(source, out);
        }
    }

    static List<String> assetPacks(Path data) throws IOException {
        Path packs = data.resolve("packs");
        List<String> names = new ArrayList<>();
        if (!Files.isDirectory(packs)) return names;
        try (Stream<Path> files = Files.list(packs)) {
            for (Iterator<Path> it = files.iterator(); it.hasNext();) {
                Path file = it.next();
                String name = file.getFileName().toString();
                if (isPackName(name) && Files.isRegularFile(file, LinkOption.NOFOLLOW_LINKS)) names.add(name);
            }
        }
        Collections.sort(names);
        return names;
    }

    static void installPack(Path data, Path stagedZip, Path stage) throws IOException {
        String name = stagedZip.getFileName().toString();
        if (!isPackName(name) || !nativeValidateAssetPack(stagedZip.toString(), stage.resolve("unpacked").toString())) {
            throw new IOException("Asset pack has no usable files under data/");
        }
        Path packs = Files.createDirectories(data.resolve("packs"));
        // An update replaces the ZIP only after the new archive has passed the game's own pack rules.
        Files.move(stagedZip, packs.resolve(name), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
    }

    private static boolean isPackName(String name) {
        return name.length() > 4 && !name.startsWith(".") && name.toLowerCase(Locale.ROOT).endsWith(".zip");
    }
}
