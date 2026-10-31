package com.protodroid;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.CancellationException;

/** Pinned, source-identified Android driver packages. No moving "latest" URL or unverified download is accepted. */
final class DriverCatalog {
    static final class Entry {
        final String id, title, description, url, sha256;
        final long size;
        final int minApi;

        Entry(String id, String title, String description, String url, long size, String sha256, int minApi) {
            this.id = id;
            this.title = title;
            this.description = description;
            this.url = url;
            this.size = size;
            this.sha256 = sha256;
            this.minApi = minApi;
        }
    }

    static final Entry[] ENTRIES = {
            new Entry(
                    "turnip-gen8-v37",
                    "Turnip Gen8 V37 — Adreno 8xx",
                    "Experimental Mesa Turnip for Adreno 810/825/829/830/840. Use only on Adreno 8xx; "
                            + "the automatic A825 build is already included in ProtoDroid.",
                    "https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases/download/v37/Turnip_Gen8_V37.zip",
                    2767183L,
                    "d0335542ec996fe33e83dfbec0d86aed301da95d22b671a88b40a4722d115b48",
                    29),
            new Entry(
                    "panvk-kbase-26.3.1",
                    "PanVK-kbase 26.3.1 — Mali (experimental)",
                    "Mesa PanVK for Mali v6/v7 and v9–v14 on mali_kbase, Android 10+, ARM64. "
                            + "Not every Mali model or game is supported; keep the system driver available.",
                    "https://github.com/zenithblue-oss/panvk-kbase-android/releases/download/"
                            + "panvk-kbase-v26.3.1/Mesa-PanVK-Android-v26.3.1-795ca7ff-adrenotools.zip",
                    9482291L,
                    "7e2e9987bbebefb06f52b2570e61e6208503d4d3a10a395b97aea9b6e1fad9e4",
                    29),
    };

    interface Progress {
        void update(long done, long total);
    }

    /** One cancellable download. The destination is a temporary file and is never treated as an installed driver. */
    static final class Download {
        private volatile boolean cancelled;
        private volatile HttpURLConnection connection;

        void cancel() {
            cancelled = true;
            HttpURLConnection current = connection;
            if (current != null) current.disconnect();
        }

        boolean isCancelled() {
            return cancelled;
        }

        void run(Entry entry, File destination, Progress progress) throws IOException {
            if (entry == null || entry.size <= 0 || !entry.sha256.matches("[0-9a-f]{64}")) {
                throw new IOException("The driver catalog entry is invalid");
            }
            HttpURLConnection current = (HttpURLConnection) new URL(entry.url).openConnection();
            connection = current;
            current.setConnectTimeout(20000);
            current.setReadTimeout(45000);
            current.setInstanceFollowRedirects(true);
            current.setRequestProperty("User-Agent", "ProtoDroid driver manager");
            try {
                if (cancelled) throw new CancellationException();
                int status = current.getResponseCode();
                if (status != HttpURLConnection.HTTP_OK) throw new IOException("Download failed (HTTP " + status + ")");
                MessageDigest digest = MessageDigest.getInstance("SHA-256");
                long done = 0;
                long lastUpdate = 0;
                byte[] buffer = new byte[64 * 1024];
                try (InputStream in = current.getInputStream(); FileOutputStream out = new FileOutputStream(destination)) {
                    for (int count; (count = in.read(buffer)) >= 0;) {
                        if (cancelled) throw new CancellationException();
                        if (count == 0) continue;
                        done += count;
                        if (done > entry.size) throw new IOException("Download is larger than the pinned package");
                        out.write(buffer, 0, count);
                        digest.update(buffer, 0, count);
                        long now = System.nanoTime();
                        if (now - lastUpdate >= 200_000_000L) {
                            lastUpdate = now;
                            progress.update(done, entry.size);
                        }
                    }
                    out.getFD().sync();
                } catch (IOException e) {
                    if (cancelled) throw new CancellationException();
                    throw e;
                }
                if (cancelled) throw new CancellationException();
                if (done != entry.size) {
                    throw new IOException("Incomplete download: " + done + " of " + entry.size + " bytes");
                }
                String actual = hex(digest.digest());
                if (!actual.equals(entry.sha256)) throw new IOException("The driver's SHA-256 checksum does not match");
                progress.update(done, entry.size);
            } catch (IOException e) {
                if (cancelled) throw new CancellationException();
                throw e;
            } catch (java.security.NoSuchAlgorithmException e) {
                throw new IOException("SHA-256 is unavailable", e);
            } finally {
                current.disconnect();
                connection = null;
            }
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) out.append(String.format(Locale.ROOT, "%02x", value & 0xff));
        return out.toString();
    }

    private DriverCatalog() {}
}
