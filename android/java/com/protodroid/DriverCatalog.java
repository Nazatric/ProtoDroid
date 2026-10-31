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
    static final String TURNIP = "turnip";
    static final String PANVK = "panvk-kbase";

    static final class Entry {
        final String id, title, description, compatibility, source, url, sha256, backend;
        final long size;
        final int minApi;

        Entry(String id, String title, String description, String compatibility, String source, String url,
              long size, String sha256, int minApi, String backend) {
            this.id = id;
            this.title = title;
            this.description = description;
            this.compatibility = compatibility;
            this.source = source;
            this.url = url;
            this.size = size;
            this.sha256 = sha256;
            this.minApi = minApi;
            this.backend = backend;
        }
    }

    /*
     * Exact release assets reviewed on 2026-10-31. Sizes and SHA-256 values come from GitHub's release-asset API.
     * These are optional downloads, never APK inputs. Keep standard, Samsung One UI, Gen8 and Mali choices separate:
     * a familiar name such as "Turnip" does not make one package interchangeable with another GPU family's driver.
     */
    static final Entry[] ENTRIES = {
            new Entry(
                    "turnip-27-standard",
                    "Turnip 27.0.0 R1",
                    "Mesa's open-source Vulkan driver for Qualcomm Adreno. Useful for A/B testing against the "
                            + "bundled Auto driver; it does not replace Android's system driver.",
                    "Adreno 6xx / 7xx  •  Android 10+",
                    "StevenMXZ / Adreno-Tools-Drivers",
                    "https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases/download/v27.0.0-R1/"
                            + "Turnip_v27.0.0-R1.zip",
                    2765466L,
                    "07938ef843a73dd47ebb88ba9c2d3bca011667bd2b657e3b4a75d9ba8ed591a0",
                    29, TURNIP),
            new Entry(
                    "turnip-27-oneui",
                    "Turnip 27.0.0 R1 · One UI",
                    "Community One UI variant for Samsung's Adreno 740 tearing issue. Try standard Turnip first on "
                            + "other devices; this is not a universal performance or black-screen fix.",
                    "Adreno 740 / Samsung One UI  •  Android 10+",
                    "StevenMXZ / Adreno-Tools-Drivers",
                    "https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases/download/v27.0.0-R1/"
                            + "Turnip_v27.0.0-R1-Oneui.zip",
                    2764954L,
                    "e5b6ac03977cfbfb26608eefcbfc5117ed2189db1ddc1687aa98ea84a3429784",
                    29, TURNIP),
            new Entry(
                    "turnip-gen8-v38",
                    "Turnip Gen8 V38",
                    "Experimental Gen8 Turnip for newer Qualcomm GPUs. Auto already includes ProtoDroid's pinned "
                            + "A825 build; download this only when you want to compare a custom package explicitly.",
                    "Adreno 810 / 825 / 829 / 830 / 840  •  Android 10+",
                    "StevenMXZ / Adreno-Tools-Drivers",
                    "https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases/download/v27.0.0-R1/"
                            + "Turnip_Gen8_V38.zip",
                    2765405L,
                    "642cb514534dc6b712ce0fb16aa76db566359b6c8542f0577f8ba6dd3aff8674",
                    29, TURNIP),
            new Entry(
                    "panvk-kbase-26.3.1",
                    "PanVK-kbase 26.3.1",
                    "Experimental Mesa Vulkan for Mali on the phone's mali_kbase kernel interface. ProtoDroid loads "
                            + "it directly as a Vulkan ICD and keeps exact BC texture decoding. Panthor/Linux packages "
                            + "are not interchangeable with this Android build.",
                    "Mali v6–v14 (v15 incomplete)  •  Android 10+  •  ARM64",
                    "zenithblue-oss / panvk-kbase-android",
                    "https://github.com/zenithblue-oss/panvk-kbase-android/releases/download/"
                            + "panvk-kbase-v26.3.1/Mesa-PanVK-Android-v26.3.1-795ca7ff-adrenotools.zip",
                    9482291L,
                    "7e2e9987bbebefb06f52b2570e61e6208503d4d3a10a395b97aea9b6e1fad9e4",
                    29, PANVK),
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
            current.setRequestProperty("Accept", "application/octet-stream");
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
