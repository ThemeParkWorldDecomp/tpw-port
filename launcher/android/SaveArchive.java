package org.tpwport.android;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Comparator;
import java.util.Enumeration;
import java.util.Iterator;
import java.util.stream.Stream;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

final class SaveArchive {
    private static final long MAX_BYTES = 512L * 1024 * 1024;
    private static final int MAX_ENTRIES = 16384;

    private SaveArchive() {}

    static void exportSaves(Path edition, Path archive) throws IOException {
        Path saves = edition.resolve("save");
        if (!Files.isDirectory(saves, LinkOption.NOFOLLOW_LINKS)) throw new IOException("No saves for this edition");
        int count = 0;
        long bytes = 0;
        try (ZipOutputStream out = new ZipOutputStream(Files.newOutputStream(archive)); Stream<Path> paths = Files.walk(saves)) {
            for (Iterator<Path> it = paths.iterator(); it.hasNext();) {
                Path source = it.next();
                if (!Files.isRegularFile(source, LinkOption.NOFOLLOW_LINKS)) continue;
                bytes += Files.size(source);
                if (++count > MAX_ENTRIES || bytes > MAX_BYTES) throw new IOException("Too many save files");
                out.putNextEntry(new ZipEntry(edition.relativize(source).toString().replace('\\', '/')));
                Files.copy(source, out);
                out.closeEntry();
            }
        }
        if (count == 0) throw new IOException("No saves for this edition");
    }

    static void restoreSaves(Path edition, Path archive, Path stage) throws IOException {
        Path incoming = Files.createDirectory(stage.resolve("save"));
        unpack(archive, incoming);

        Path saves = edition.resolve("save");
        Path previous = edition.resolve(".save-before-import");
        Files.createDirectories(edition);
        recoverSaves(edition);
        removeTree(previous);
        if (Files.exists(saves)) Files.move(saves, previous, StandardCopyOption.ATOMIC_MOVE);
        try {
            Files.move(incoming, saves, StandardCopyOption.ATOMIC_MOVE);
        } catch (IOException error) {
            recoverSaves(edition);
            throw error;
        }
        // A crash before the new save directory was installed leaves the old one recoverable.
        // With the new directory installed, the old copy will be cleared by the next restore.
    }

    static void recoverSaves(Path edition) throws IOException {
        Path saves = edition.resolve("save");
        Path previous = edition.resolve(".save-before-import");
        if (!Files.exists(saves) && Files.isDirectory(previous)) {
            Files.move(previous, saves, StandardCopyOption.ATOMIC_MOVE);
        }
    }

    private static void unpack(Path archive, Path incoming) throws IOException {
        int files = 0;
        long total = 0;
        byte[] buffer = new byte[65536];
        try (ZipFile zip = new ZipFile(archive.toFile())) {
            if (zip.size() > MAX_ENTRIES) throw new IOException("Too many archive entries");
            for (Enumeration<? extends ZipEntry> entries = zip.entries(); entries.hasMoreElements();) {
                ZipEntry item = entries.nextElement();
                String name = item.getName();
                if (name.equals("save/") && item.isDirectory()) continue;
                if (!name.startsWith("save/") || name.indexOf('\\') >= 0 || name.indexOf(':') >= 0) {
                    throw new IOException("Expected a ZIP of the save folder");
                }
                Path path = incoming.resolve(name.substring(5));
                if (!path.equals(path.normalize()) || !path.startsWith(incoming) || path.equals(incoming)) {
                    throw new IOException("Invalid save path");
                }
                if (item.isDirectory()) continue;
                if (item.getSize() < 0 || item.getSize() > MAX_BYTES - total) throw new IOException("Save archive is too large");
                Files.createDirectories(path.getParent());
                if (Files.exists(path)) throw new IOException("Repeated save path");
                CRC32 crc = new CRC32();
                long size = 0;
                try (InputStream in = zip.getInputStream(item); OutputStream out = Files.newOutputStream(path)) {
                    for (int read; (read = in.read(buffer)) != -1;) {
                        size += read;
                        if (size > item.getSize()) throw new IOException("Invalid save size");
                        out.write(buffer, 0, read);
                        crc.update(buffer, 0, read);
                    }
                }
                if (size != item.getSize() || crc.getValue() != item.getCrc()) throw new IOException("Damaged save file");
                total += size;
                ++files;
            }
        }
        if (files == 0) throw new IOException("Save archive is empty");
    }

    private static void removeTree(Path directory) throws IOException {
        if (!Files.exists(directory)) return;
        try (Stream<Path> paths = Files.walk(directory)) {
            Iterator<Path> it = paths.sorted(Comparator.reverseOrder()).iterator();
            while (it.hasNext()) Files.delete(it.next());
        }
    }
}
