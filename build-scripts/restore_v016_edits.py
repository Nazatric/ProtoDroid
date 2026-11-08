#!/usr/bin/env python3
"""Re-apply the V1.3 + 0.16-latest source edits after a sandbox rollback to the V1 tree.
Idempotent: every edit asserts its old text is present and its new text is not already applied."""
import re, sys

ROOT = "/home/z/my-project/work/v1"

def patch(path, old, new, tag):
    s = open(path).read()
    if new in s and old not in s:
        print(f"skip {tag} (already applied)")
        return
    assert old in s, f"{tag}: old text not found in {path}"
    s = s.replace(old, new, 1)
    open(path, "w").write(s)
    print(f"ok {tag}")

# ---------- win.h: OnClose hook, refs removed ----------
patch(f"{ROOT}/src/native/win.h",
"""    // Wakes the threads waiting for this object; g_kernel_mutex is held.
    void Notify() {
        for (KWaiter* w : waiters) w->cv.notify_one();
    }
    KType type;
    // Atomic because the lock-free fast paths pin an object against a concurrent CloseHandle
    // (see HandleObjectPinned in kernel32.cpp); every reference transition happens under
    // g_handles_mutex, which is what makes object lifetime exact.
    std::atomic<int> refs{1};
    std::vector<KWaiter*> waiters;  // under g_kernel_mutex""",
"""    // Kernel objects are immortal: CloseHandle detaches them from the handle table and calls
    // this once, but the memory is never freed, so wait/signal fast paths can use the object
    // after a bare atomic load with no lifetime protocol at all. File overrides this to close
    // its fd (the one bounded resource); the discarded shell then just idles in the heap.
    virtual void OnClose() {}
    // Wakes the threads waiting for this object; g_kernel_mutex is held.
    void Notify() {
        for (KWaiter* w : waiters) w->cv.notify_one();
    }
    KType type;
    std::vector<KWaiter*> waiters;  // under g_kernel_mutex""",
"win.h OnClose")

# ---------- kernel32.cpp: pin/unpin/close ----------
p = f"{ROOT}/src/native/kernel32.cpp"
s = open(p).read()
i1 = s.find("KObject* HandleObjectPinned(uint32_t handle) {")
j1 = s.find("void SetLastError(uint32_t code)")
assert i1 > 0 and j1 > i1, "kernel32 pin region anchors"
new_block = """// Pins the object for the lock-free fast paths. Objects are immortal (CloseHandle detaches and
// calls OnClose, never frees), so pinning is a bare atomic load: no g_handles_mutex on the hot
// path, no use-after-free window, and no lifetime protocol for callers to get wrong. The old
// refcount-under-global-mutex scheme cost two contended lock round-trips per wait and per
// signal \u2014 the single biggest sync cost in the profiled sustained phases.
KObject* HandleObjectPinned(uint32_t handle) {
    return HandleObject(handle);
}

void ReleasePinnedObject(KObject*) {
    // Immortal objects: nothing to release.
}

void CloseKernelHandle(uint32_t handle) {
    if (handle < 0x100 || (handle & 3)) return;
    uint32_t i = (handle - 0x100) / 4;
    if (i >= kMaxHandles) return;
    // The exchange is the one winner: exactly one close runs OnClose (File releases its fd),
    // later closes see null. The object itself outlives every waiter and reader by design.
    KObject* obj = g_handles[i].exchange(nullptr, std::memory_order_acq_rel);
    if (obj) obj->OnClose();
}

"""
if "KObject* obj = g_handles[i].exchange(nullptr, std::memory_order_acq_rel);" not in s:
    s = s[:i1] + new_block + s[j1:]
    print("ok kernel32 pin/unpin/close")
else:
    print("skip kernel32 pin/unpin/close")

patch(p,
"""struct File : KObject {
    File() : KObject(KType::File) {}
    ~File() override {
        if (fd >= 0) close(fd);
    }
    int fd = -1;""",
"""struct File : KObject {
    File() : KObject(KType::File) {}
    // Immortal like every kernel object, but the fd is the one bounded resource: CloseKernelHandle
    // calls OnClose once (the handle-table exchange is the one winner), which releases it. The
    // destructor never runs, so closing here \u2014 not there \u2014 is what keeps the fd count flat.
    void OnClose() override {
        detached.store(true, std::memory_order_release);
        int expected = fd.exchange(-1, std::memory_order_acq_rel);
        if (expected >= 0) close(expected);
    }
    std::atomic<int> fd{-1};
    std::atomic<bool> detached{false};""",
"kernel32 File OnClose")

patch(p,
"""File* FileOf(uint32_t h) {
    // Pinned: the caller dereferences the File across the whole read/write syscall, and a
    // concurrent CloseHandle from the engine's loader thread must not free it mid-operation.
    KObject* o = HandleObjectPinned(h);
    if (o && o->type == KType::File) return static_cast<File*>(o);
    ReleasePinnedObject(o);
    return nullptr;
}""",
"""File* FileOf(uint32_t h) {
    // Objects are immortal, so the returned pointer is valid no matter what; the detached flag
    // keeps a closed File's fd number from being read after CloseHandle released it (the freed
    // number could already belong to a different open \u2014 Windows turns the same guest race into
    // an invalid-handle error, and so does this).
    KObject* o = HandleObjectPinned(h);
    if (o && o->type == KType::File && !static_cast<File*>(o)->detached.load(std::memory_order_acquire))
        return static_cast<File*>(o);
    return nullptr;
}""",
"kernel32 FileOf")

patch(p,
"""    auto* t = new Thread();
    t->refs = 2;""",
"""    auto* t = new Thread();""",
"kernel32 Thread refs")

patch(p, "    f->fd = fd;", "    f->fd.store(fd, std::memory_order_relaxed);", "kernel32 fd store")

# ---------- ProtoActivity ----------
a = f"{ROOT}/android/java/com/protodroid/ProtoActivity.java"
s = open(a).read()
if "private ProtoSurface gameSurface;" not in s:
    old = """import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.Arrays;
import java.util.List;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/** SDL's activity running the native port (libmain.so: the recompiled engine and its platform layer). */
public class ProtoActivity extends SDLActivity {
"""
    new = """import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.view.Display;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/** SDL's activity running the native port (libmain.so: the recompiled engine and its platform layer). */
public class ProtoActivity extends SDLActivity {
    private ProtoSurface gameSurface;
"""
    assert old in s, "ProtoActivity header block"
    s = s.replace(old, new, 1)
    print("ok ProtoActivity imports/field")
else:
    print("skip ProtoActivity imports/field")

patch(a,
"""    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new ProtoSurface(context);
    }""",
"""    @Override
    protected SDLSurface createSDLSurface(Context context) {
        gameSurface = new ProtoSurface(context);
        return gameSurface;
    }

    /**
     * Pin the display to its highest refresh rate at the current resolution. Without this, the power
     * manager down-switches the panel (120 -> 60 Hz and back) whenever the game's frame load changes,
     * and each switch tears down the BufferQueue: the Vulkan swapchain has to be recreated while the
     * old buffers drain, which stalled the whole game for ~20 seconds in the field. A pinned mode
     * means no switches, so no teardowns.
     */
    private void pinDisplayMode() {
        try {
            Display display = getWindowManager().getDefaultDisplay();
            Display.Mode current = display.getMode();
            Display.Mode best = current;
            for (Display.Mode m : display.getSupportedModes()) {
                if (m.getPhysicalWidth() == current.getPhysicalWidth()
                        && m.getPhysicalHeight() == current.getPhysicalHeight()
                        && m.getRefreshRate() > best.getRefreshRate()) {
                    best = m;
                }
            }
            if (best.getModeId() != current.getModeId()) {
                WindowManager.LayoutParams p = getWindow().getAttributes();
                p.preferredDisplayModeId = best.getModeId();
                getWindow().setAttributes(p);
            }
        } catch (Exception ignored) {
            // Without the pin the system picks rates as before; the game still runs.
        }
    }

    /**
     * Install the bundled minimap mod on first run so the in-game MODS panel has something to edit
     * from day one. Present = the user's copy wins (they may have edited or disabled it); the game
     * runs fine either way, so any failure here is silently non-fatal.
     */
    private void ensureBundledMods() {
        File mods = ModManager.modsDir(this);
        File target = new File(mods, "protodroid-minimap");
        if (new File(target, "protodroid-mod.ini").isFile()) return;
        try (InputStream in = getAssets().open("mods/protodroid-minimap.zip")) {
            ZipInputStream zip = new ZipInputStream(in);
            byte[] buffer = new byte[16384];
            for (ZipEntry e; (e = zip.getNextEntry()) != null; ) {
                File out = new File(mods, e.getName());
                if (!out.getCanonicalPath().startsWith(mods.getCanonicalPath() + File.separator)) continue;
                if (e.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                out.getParentFile().mkdirs();
                try (FileOutputStream f = new FileOutputStream(out)) {
                    for (int n; (n = zip.read(buffer)) > 0; ) f.write(buffer, 0, n);
                }
                zip.closeEntry();
            }
            new File(target, "enabled").createNewFile();
        } catch (IOException ignored) {
            // The mod stays optional; the game runs without it.
        }
    }""",
"ProtoActivity pin + installer")

patch(a,
"""        prepareVulkanDriver();
        ModManager.prepareForLaunch(this);
        // The native side loads the bundled Vulkan driver (Turnip, on Adreno) from the extracted libraries.""",
"""        prepareVulkanDriver();
        ModManager.prepareForLaunch(this);
        ensureBundledMods();
        // The native side loads the bundled Vulkan driver (Turnip, on Adreno) from the extracted libraries.""",
"ProtoActivity onCreate installer call")

patch(a,
"""        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);  // no dimming while playing""",
"""        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);  // no dimming while playing
        pinDisplayMode();""",
"ProtoActivity onCreate pin call")

# ---------- manifest ----------
m = f"{ROOT}/android/AndroidManifest.xml"
s = open(m).read()
s = re.sub(r'android:versionCode="\\d+"', 'android:versionCode="33"', s, count=1)
s = re.sub(r'android:versionName="[^"]*"', 'android:versionName="0.16"', s, count=1)
open(m, "w").write(s)
print("ok manifest 33 / 0.16")

print("ALL SOURCE EDITS RESTORED")
