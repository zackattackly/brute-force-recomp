// Exports the whole of Brute Force (default.xbe) as decompiled C, one file per subsystem,
// using every name and comment the BruteForce_ApplyNames script has applied.
// Run BruteForce_ApplyNames.java first. Output: <your user folder>\Brute Force data\decompiled
// (outside OneDrive). Takes a while (every function is decompiled); the progress bar shows how far it is.
//@category BruteForce
//@description Decompile every function to C files grouped by subsystem (writes to Brute Force data\decompiled).
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.DecompiledFunction;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.Array;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeWriter;
import ghidra.program.model.listing.CommentType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolType;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class BruteForce_ExportC extends GhidraScript {

    // Library and runtime ranges (see README: "Library map").
    private static final long[][] RANGES = {
        {0x2B2B10L, 0x2DF610L}, {0x2EA000L, 0x2F2000L}, {0x2F2000L, 0x2F9000L}, {0x2F9000L, 0x3118C0L},
        {0x3118C0L, 0x330020L}, {0x330020L, 0x344F20L}, {0x344F20L, 0x356C80L}, {0x356C80L, 0x36C5C0L},
        {0x36C5C0L, 0x37ECA0L}, {0x37ECA0L, 0x386340L}};
    private static final String[] RANGE_NAMES = {
        "lib_physics_ipion", "lib_crt", "lib_xapi", "lib_runtime_misc",
        "lib_dsound", "lib_xonline", "lib_xnet", "lib_bink",
        "lib_d3d8", "lib_xpp_input"};

    private static final int MIN_GROUP = 4;      // named prefixes with fewer functions go to game_misc_named
    private static final int TIMEOUT_SECS = 60;  // per function

    private String rawGroup(Function f) {
        long a = f.getEntryPoint().getOffset();
        for (int i = 0; i < RANGES.length; i++) {
            if (a >= RANGES[i][0] && a < RANGES[i][1]) return RANGE_NAMES[i];
        }
        String n = f.getName();
        if (n.startsWith("lua") || n.startsWith("luaL_") || n.startsWith("luaB_")) return "lib_lua";
        if (f.getSymbol().getSource() == SourceType.DEFAULT || n.startsWith("FUN_") || n.startsWith("ivp_")) {
            // unnamed game code: one file per 64 KB of address space
            return String.format("game_unnamed_%06X", a & ~0xFFFFL);
        }
        int us = n.indexOf('_');
        String p = us > 0 ? n.substring(0, us) : n;
        p = p.replaceAll("[^A-Za-z0-9]", "");
        if (p.isEmpty()) p = "misc";
        return "game_" + p;
    }

    private static String clean(String s) {
        return s == null ? "" : s.replace("*/", "* /");
    }

    @Override
    public void run() throws Exception {
        File out = new File(new File(System.getProperty("user.home"), "Brute Force data"), "decompiled");
        if (!out.isDirectory() && !out.mkdirs()) {
            println("Could not create " + out);
            return;
        }

        // Group all functions.
        Map<String, List<Function>> groups = new TreeMap<>();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int total = 0;
        while (it.hasNext()) {
            Function f = it.next();
            if (f.isExternal()) continue;
            groups.computeIfAbsent(rawGroup(f), k -> new ArrayList<>()).add(f);
            total++;
        }
        // Tiny named groups (fewer than MIN_GROUP functions) go to a few shared files by kind.
        Map<String, List<Function>> merged = new TreeMap<>();
        for (Map.Entry<String, List<Function>> e : groups.entrySet()) {
            String g = e.getKey();
            if (g.startsWith("game_") && !g.startsWith("game_unnamed_") && e.getValue().size() < MIN_GROUP) {
                for (Function f : e.getValue()) {
                    merged.computeIfAbsent(smallGroup(f), k -> new ArrayList<>()).add(f);
                }
                continue;
            }
            merged.computeIfAbsent(g, k -> new ArrayList<>()).addAll(e.getValue());
        }

        writeTypesHeader(new File(out, "ghidra_types.h"));
        int nGlobals = writeGlobalsHeader(new File(out, "bruteforce_globals.h"));
        try (PrintWriter w = writer(new File(out, "bruteforce_types.h"))) {
            w.println("/* Data types defined in the Ghidra program (structures, enums, typedefs). */");
            w.println("#pragma once");
            w.println("#include \"ghidra_types.h\"");
            new DataTypeWriter(currentProgram.getDataTypeManager(), w, false)
                .write(currentProgram.getDataTypeManager(), monitor);
        }

        DecompInterface di = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        opts.grabFromProgram(currentProgram);
        di.setOptions(opts);
        di.toggleCCode(true);
        di.toggleSyntaxTree(false);
        di.setSimplificationStyle("decompile");
        if (!di.openProgram(currentProgram)) {
            println("Decompiler failed to start: " + di.getLastMessage());
            return;
        }

        monitor.initialize(total);
        monitor.setMessage("Decompiling Brute Force");
        int done = 0, failed = 0;
        PrintWriter index = writer(new File(out, "index.csv"));
        index.println("address,name,file,status");
        try {
            for (Map.Entry<String, List<Function>> e : merged.entrySet()) {
                String file = e.getKey() + ".c";
                try (PrintWriter w = writer(new File(out, file))) {
                    w.println("/* Brute Force (Xbox, 2003) - decompiled by Ghidra from default.xbe.");
                    w.println(" * Group: " + e.getKey() + " (" + e.getValue().size() + " functions).");
                    w.println(" * Names and comments come from the Brute Force recomp project; most are inferred.");
                    w.println(" * This is Ghidra's reading of the machine code, not the original source. */");
                    w.println("#include \"ghidra_types.h\"");
                    w.println("#include \"bruteforce_types.h\"");
                    w.println("#include \"bruteforce_globals.h\"");
                    w.println();
                    for (Function f : e.getValue()) {
                        monitor.checkCancelled();
                        monitor.setMessage("Decompiling " + f.getName());
                        String addr = String.format("0x%06X", f.getEntryPoint().getOffset());
                        String status = "ok";
                        w.println("/* " + addr + "  " + clean(f.getName()) + " */");
                        DecompileResults r = di.decompileFunction(f, TIMEOUT_SECS, monitor);
                        DecompiledFunction df = (r != null && r.decompileCompleted()) ? r.getDecompiledFunction() : null;
                        if (df != null && df.getC() != null) {
                            w.println(df.getC());
                        } else {
                            failed++;
                            status = "failed";
                            String msg = r == null ? "no result" : clean(r.getErrorMessage());
                            w.println("/* decompile failed: " + msg.trim() + " */");
                            w.println();
                        }
                        index.println(addr + "," + f.getName().replace(',', ';') + "," + file + "," + status);
                        done++;
                        monitor.incrementProgress(1);
                    }
                }
            }
        } finally {
            index.close();
            di.dispose();
        }
        println("Brute Force C export: " + done + " functions in " + merged.size() + " files, "
            + failed + " failed, " + nGlobals + " globals. Folder: " + out.getAbsolutePath());
    }

    // Where a function from a tiny named group goes.
    private String smallGroup(Function f) {
        long a = f.getEntryPoint().getOffset();
        String n = f.getName();
        int us = n.indexOf('_');
        String p = us > 0 ? n.substring(0, us) : n;
        if (a >= 0xA9000L && a < 0xCD000L) return "game_hud";
        if (n.contains("QueryInterface") || p.equals("Com") || p.equals("ComPtr") || p.equals("TypeRegistry")) return "game_components";
        if (p.matches(".*(Archetype|Data|Type|Library|Libraries|List|Lib|Params|Info|Def|File)$")
            || n.endsWith("_ReadProperty") || n.endsWith("_GetTypeId") || n.endsWith("_Create")) return "game_data_types";
        if (p.matches("(std|Vector|Vector4|Vector8|List|Matrix34|Matrix3|Mat3|Vec3|Pool|Property|Name|String|Ref|Array|Hash|Crc)")) return "game_util";
        return "game_misc_named";
    }

    // extern declarations for every named global (labels outside functions).
    private int writeGlobalsHeader(File f) throws Exception {
        Listing listing = currentProgram.getListing();
        int n = 0;
        try (PrintWriter w = writer(f)) {
            w.println("/* Named globals in default.xbe, with their addresses and notes from the Brute Force recomp project. */");
            w.println("#pragma once");
            w.println("#include \"ghidra_types.h\"");
            w.println("#include \"bruteforce_types.h\"");
            w.println();
            SymbolIterator si = currentProgram.getSymbolTable().getDefinedSymbols();
            while (si.hasNext()) {
                monitor.checkCancelled();
                Symbol s = si.next();
                if (s.getSymbolType() != SymbolType.LABEL || s.getSource() == SourceType.DEFAULT || s.isExternal()) continue;
                Address addr = s.getAddress();
                if (!addr.isMemoryAddress()) continue;
                if (currentProgram.getFunctionManager().getFunctionContaining(addr) != null) continue;
                String name = s.getName();
                if (!name.matches("[A-Za-z_][A-Za-z0-9_]*")) continue;
                String decl;
                Data d = listing.getDataAt(addr);
                DataType dt = d == null ? null : d.getDataType();
                if (dt instanceof Array) {
                    Array arr = (Array) dt;
                    decl = arr.getDataType().getDisplayName() + " " + name + "[" + arr.getNumElements() + "]";
                } else if (dt != null) {
                    decl = dt.getDisplayName() + " " + name;
                } else {
                    decl = "undefined " + name;
                }
                String c = listing.getComment(CommentType.PLATE, addr);
                if (c == null) c = listing.getComment(CommentType.EOL, addr);
                if (c != null && !c.isEmpty()) {
                    w.println("/* " + clean(c).replace("\n", "\n   ") + " */");
                }
                w.println("extern " + decl + "; /* " + String.format("0x%06X", addr.getOffset()) + " */");
                n++;
            }
        }
        return n;
    }

    private static PrintWriter writer(File f) throws Exception {
        return new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), StandardCharsets.UTF_8));
    }

    private static void writeTypesHeader(File f) throws Exception {
        try (PrintWriter w = writer(f)) {
            w.println("/* Types used by Ghidra's decompiler output (32-bit x86, Xbox). */");
            w.println("#pragma once");
            w.println("typedef unsigned char   undefined;");
            w.println("typedef unsigned char   undefined1;");
            w.println("typedef unsigned short  undefined2;");
            w.println("typedef unsigned int    undefined3;");
            w.println("typedef unsigned int    undefined4;");
            w.println("typedef unsigned long long undefined6;");
            w.println("typedef unsigned long long undefined8;");
            w.println("typedef unsigned char   byte;");
            w.println("typedef unsigned char   uchar;");
            w.println("typedef unsigned short  ushort;");
            w.println("typedef unsigned short  word;");
            w.println("typedef unsigned int    uint;");
            w.println("typedef unsigned int    dword;");
            w.println("typedef unsigned long   ulong;");
            w.println("typedef unsigned long long ulonglong;");
            w.println("typedef long long       longlong;");
            w.println("typedef unsigned int    uint3;");
            w.println("typedef long double     float10;");
            w.println("typedef unsigned char   bool;");
            w.println("typedef void           *pointer;");
            w.println("typedef unsigned short  wchar16;");
            w.println("typedef unsigned short  wchar_t;");
            w.println("typedef int             code;");
        }
    }
}
