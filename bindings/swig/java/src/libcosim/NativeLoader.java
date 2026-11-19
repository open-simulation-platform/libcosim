package libcosim;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;

/**
 * Loads libcosim's native libraries. Called automatically by the generated
 * JNI class.
 *
 * In the libcosim JAR, the native libraries for each platform are bundled
 * under /libcosim/native/&lt;os&gt;-&lt;arch&gt;/ with an index.txt; they are
 * extracted to a temporary directory and loaded from there. Outside a JAR
 * (e.g. a build tree), cosim_java is loaded from java.library.path.
 */
public final class NativeLoader {
    private static boolean loaded = false;

    private NativeLoader() {}

    public static synchronized void load() {
        if (loaded) return;
        String base = "/libcosim/native/" + platform() + "/";
        InputStream index = NativeLoader.class.getResourceAsStream(base + "index.txt");
        if (index == null) {
            System.loadLibrary("cosim_java");
        } else {
            try {
                loadBundled(base, index);
            } catch (IOException e) {
                throw new UnsatisfiedLinkError("Could not extract libcosim's native libraries: " + e);
            }
        }
        loaded = true;
    }

    static String platform() {
        String os = System.getProperty("os.name").toLowerCase(Locale.ROOT);
        String osName = os.contains("win") ? "windows" : os.contains("mac") ? "macos" : "linux";
        String arch = System.getProperty("os.arch");
        String archName = (arch.equals("amd64") || arch.equals("x86_64")) ? "x86_64" : arch;
        return osName + "-" + archName;
    }

    private static void loadBundled(String base, InputStream index) throws IOException {
        List<String> names = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(index, StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (!line.trim().isEmpty()) names.add(line.trim());
            }
        }
        Path dir = Files.createTempDirectory("libcosim-native-");
        dir.toFile().deleteOnExit();
        for (String name : names) {
            try (InputStream in = NativeLoader.class.getResourceAsStream(base + name)) {
                Path target = dir.resolve(name);
                Files.copy(in, target, StandardCopyOption.REPLACE_EXISTING);
                target.toFile().deleteOnExit();
            }
        }
        String main = System.mapLibraryName("cosim_java");
        if (platform().startsWith("windows")) {
            // Windows does not search a DLL's own directory for the DLLs it
            // depends on, so load the dependencies first, in an order that works.
            List<String> pending = new ArrayList<>(names);
            pending.remove(main);
            UnsatisfiedLinkError last = null;
            boolean progress = true;
            while (!pending.isEmpty() && progress) {
                progress = false;
                for (Iterator<String> it = pending.iterator(); it.hasNext(); ) {
                    String name = it.next();
                    try {
                        System.load(dir.resolve(name).toString());
                        it.remove();
                        progress = true;
                    } catch (UnsatisfiedLinkError e) {
                        last = e;
                    }
                }
            }
            if (!pending.isEmpty()) throw last;
        }
        // On Linux the libraries find each other through RPATH $ORIGIN.
        System.load(dir.resolve(main).toString());
    }
}
